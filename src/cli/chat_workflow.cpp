#include "chat_workflow.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <iostream>
#include <memory>

#include "backend/core/model.hpp"
#include "backend/core/thinking_stripper.hpp"
#include "cli_common.hpp"

namespace dir2md::cli {
namespace {

inline const QString language_model_endpoint_key = "language-model/endpoint";
inline const QString language_model_name_key = "language-model/model-name";

auto print_chat_usage() -> void {
    std::cerr <<
        "Usage: dir2md_cli chat [options]\n\n"
        "Options:\n"
        "  --prompt <text>       Prompt to send (mandatory)\n"
        "  --system <text|file>  System prompt: literal text or path to a readable file\n"
        "  --temperature <0-2>   Sampling temperature in [0.0, 2.0]\n"
        "  --help, -h            Show this help\n";
}

} // namespace

auto execute_chat(const QStringList &args) -> int {
    QCommandLineParser parser;
    parser.setApplicationDescription("Chat with the language model; streams the reply to stdout.");
    const QCommandLineOption help_opt = parser.addHelpOption();

    QCommandLineOption prompt_opt("prompt", "Prompt to send (mandatory).", "text");
    QCommandLineOption system_opt("system", "System prompt: literal text or path to a readable file.", "text");
    QCommandLineOption temperature_opt("temperature", "Sampling temperature in [0.0, 2.0].", "value");
    parser.addOption(prompt_opt);
    parser.addOption(system_opt);
    parser.addOption(temperature_opt);

    // parse() treats the first element as the program name.
    QStringList parse_args = args;
    parse_args.prepend("chat");

    if (!parser.parse(parse_args)) {
        std::cerr << parser.errorText().toStdString() << "\n";
        print_chat_usage();
        return 1;
    }
    if (parser.isSet(help_opt)) {
        std::cout << parser.helpText().toStdString();
        return 0;
    }

    // --prompt is mandatory
    if (!parser.isSet(prompt_opt)) {
        std::cerr << "Error: --prompt is mandatory for the chat workflow.\n";
        print_chat_usage();
        return 1;
    }
    const QString user_prompt = parser.value(prompt_opt);

    auto settings = bootstrap_settings(QCoreApplication::instance());

    auto system_result = resolve_system_prompt(parser.value(system_opt), *settings);
    if (!system_result.has_value()) {
        std::cerr << "Error: " << system_result.error().description.toStdString() << "\n";
        return 1;
    }
    const QString system_prompt = system_result.value();

    std::optional<QString> temperature_option =
        parser.isSet(temperature_opt) ? std::optional<QString>(parser.value(temperature_opt)) : std::nullopt;
    auto temperature_result = resolve_temperature(temperature_option, *settings);
    if (!temperature_result.has_value()) {
        std::cerr << "Error: " << temperature_result.error().description.toStdString() << "\n";
        return 1;
    }
    const double temperature = temperature_result.value();

    auto endpoint_result = resolve_endpoint(language_model_endpoint_key, *settings);
    if (!endpoint_result.has_value()) {
        std::cerr << "Error: " << endpoint_result.error().description.toStdString() << "\n";
        return 1;
    }

    auto model_name_result = resolve_model_name(language_model_name_key, *settings);
    if (!model_name_result.has_value()) {
        std::cerr << "Error: " << model_name_result.error().description.toStdString() << "\n";
        return 1;
    }
    const QString model_name = model_name_result.value();

    // Header: effective temperature + full system prompt + full user prompt.
    std::cout << "temperature: " << temperature << "\n";
    std::cout << "system prompt:\n" << system_prompt.toStdString() << "\n";
    std::cout << "user prompt:\n" << user_prompt.toStdString() << "\n\n";

    auto client = std::make_unique<dir2md::backend::text_to_text_client>(QCoreApplication::instance());
    client->set_endpoint_url(endpoint_result.value());
    client->set_model_name(model_name);
    client->set_temperature(static_cast<float>(temperature));
    client->set_system_prompt(system_prompt);
    client->set_user_prompt(user_prompt);

    dir2md::backend::thinking_stripper stripper;
    int exit_code = 0;
    bool finished = false;

    QObject::connect(client.get(), &dir2md::backend::model_client_base::incremental_chunk,
                     [&stripper](const QString &chunk) {
        const QString text = stripper.process(chunk);
        if (!text.isEmpty()) {
            std::cout << text.toStdString() << std::flush;
        }
    });
    QObject::connect(client.get(), &dir2md::backend::model_client_base::completion,
                     [&stripper, &exit_code, &finished]() {
        const QString tail = stripper.flush();
        if (!tail.isEmpty()) {
            std::cout << tail.toStdString() << std::flush;
        }
        std::cout << "\n" << std::flush;
        exit_code = 0;
        finished = true;
    });
    QObject::connect(client.get(), &dir2md::backend::model_client_base::error_occurred,
                     [&stripper, &exit_code, &finished](const dir2md::backend::error_frame &err) {
        const QString tail = stripper.flush();
        if (!tail.isEmpty()) {
            std::cout << tail.toStdString() << std::flush;
        }
        std::cerr << "\nError: model request failed (" << err.error_code << "): "
                  << err.description.toStdString() << "\n";
        exit_code = 1;
        finished = true;
    });

    client->send_request();

    // Drive the Qt event loop while the reply streams.
    while (!finished && QCoreApplication::instance()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    return exit_code;
}

} // namespace dir2md::cli
