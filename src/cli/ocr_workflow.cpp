#include "ocr_workflow.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "backend/core/model.hpp"
#include "backend/core/pdf_renderer.hpp"
#include "backend/core/thinking_stripper.hpp"
#include "cli_common.hpp"

namespace dir2md::cli {
namespace {

inline const QString ocr_model_endpoint_key = "ocr-model/endpoint";
inline const QString ocr_model_name_key = "ocr-model/model-name";

auto print_ocr_usage() -> void {
    std::cerr <<
        "Usage: dir2md_cli ocr [options]\n\n"
        "Options:\n"
        "  --source <path>       Source file or folder (mandatory)\n"
        "  --system <text|file>  System prompt: literal text or path to a readable file\n"
        "  --temperature <0-2>   Sampling temperature in [0.0, 2.0]\n"
        "  --output <folder>     Output folder for markdown files (default: alongside source)\n"
        "  --yes, -y             Skip the folder confirmation prompt\n"
        "  --help, -h            Show this help\n";
}

auto supported_extensions() -> QStringList {
    return { "jpg", "jpeg", "png", "pdf" };
}

auto has_supported_extension(const QString &path) -> bool {
    return supported_extensions().contains(QFileInfo(path).suffix().toLower());
}

// Non-recursive top-level scan for supported files (case-insensitive).
auto scan_folder(const QString &folder_path) -> QStringList {
    QDir dir(folder_path);
    QStringList result;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &info : entries) {
        if (has_supported_extension(info.fileName())) {
            result << info.absoluteFilePath();
        }
    }
    return result;
}

struct request_result {
    bool success = false;
    QString content;  // stripped text, indicator-free (file-safe)
};

// Attach the streaming handlers to a client: each incremental chunk is routed
// through the thinking stripper to stdout (flushed) and accumulated. The
// caller must invoke this before send_request so early errors are captured.
auto attach_ocr_streaming(dir2md::backend::image_to_text_client &client,
                          dir2md::backend::thinking_stripper &stripper,
                          request_result &result, bool &finished) -> void {
    QString emitted;  // stripped text including the thinking indicator

    QObject::connect(&client, &dir2md::backend::model_client_base::incremental_chunk,
                     [&stripper, &emitted](const QString &chunk) {
        const QString text = stripper.process(chunk);
        emitted += text;
        if (!text.isEmpty()) {
            std::cout << text.toStdString() << std::flush;
        }
    });
    QObject::connect(&client, &dir2md::backend::model_client_base::completion,
                     [&stripper, &emitted, &result, &finished](const QString &, const dir2md::backend::token_stats &) {
        const QString tail = stripper.flush();
        emitted += tail;
        if (!tail.isEmpty()) {
            std::cout << tail.toStdString() << std::flush;
        }
        result.content = emitted.remove(dir2md::backend::thinking_indicator);
        result.success = true;
        finished = true;
    });
    QObject::connect(&client, &dir2md::backend::model_client_base::error_occurred,
                     [&stripper, &emitted, &result, &finished](const dir2md::backend::error_frame &err) {
        const QString tail = stripper.flush();
        emitted += tail;
        if (!tail.isEmpty()) {
            std::cout << tail.toStdString() << std::flush;
        }
        std::cerr << "Error: model request failed (" << err.error_code << "): "
                  << err.description.toStdString() << "\n";
        result.success = false;
        finished = true;
    });
}

// Drive the Qt event loop until the request finishes (or the app is gone).
auto pump_until_finished(bool &finished) -> void {
    while (!finished && QCoreApplication::instance()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

// Process one source (image file or PDF) sequentially. Returns success and the
// markdown content for the source (pages assembled with separators for PDFs).
auto process_source(const QString &source_path, const QString &endpoint, float temperature,
                    const QString &system_prompt, bool is_pdf,
                    const QString &model_name) -> std::pair<bool, QString> {
    dir2md::backend::thinking_stripper stripper;

    if (!is_pdf) {
        auto client = std::make_unique<dir2md::backend::image_to_text_client>(QCoreApplication::instance());
        client->set_endpoint_url(endpoint);
        client->set_model_name(model_name);
        client->set_temperature(temperature);
        request_result result;
        bool finished = false;
        attach_ocr_streaming(*client, stripper, result, finished);
        client->send_request(source_path, system_prompt);
        pump_until_finished(finished);
        return { result.success, result.content };
    }

    dir2md::backend::pdf_renderer renderer;
    auto opened = renderer.open(source_path);
    if (!opened.has_value()) {
        std::cerr << "Error: " << opened.error().description.toStdString() << "\n";
        return { false, QString() };
    }

    QStringList pages;
    for (int i = 0; i < renderer.page_count(); ++i) {
        // Render one page at a time; the image is released when it goes out of
        // scope at the end of this iteration (after its OCR completes).
        auto page_image = renderer.render_page(i);
        if (!page_image.has_value()) {
            std::cerr << "Error: " << page_image.error().description.toStdString() << "\n";
            return { false, QString() };
        }

        stripper.reset();
        auto client = std::make_unique<dir2md::backend::image_to_text_client>(QCoreApplication::instance());
        client->set_endpoint_url(endpoint);
        client->set_model_name(model_name);
        client->set_temperature(temperature);
        request_result result;
        bool finished = false;
        attach_ocr_streaming(*client, stripper, result, finished);
        client->send_request(page_image.value(), system_prompt);
        pump_until_finished(finished);
        if (!result.success) {
            return { false, QString() };
        }
        pages << result.content;
    }

    return { true, assemble_pdf_markdown(pages) };
}

} // namespace

auto execute_ocr(const QStringList &args) -> int {
    QCommandLineParser parser;
    parser.setApplicationDescription("OCR images and PDFs into markdown files.");
    const QCommandLineOption help_opt = parser.addHelpOption();

    QCommandLineOption source_opt("source", "Source file or folder (mandatory).", "path");
    QCommandLineOption system_opt("system", "System prompt: literal text or path to a readable file.", "text");
    QCommandLineOption temperature_opt("temperature", "Sampling temperature in [0.0, 2.0].", "value");
    QCommandLineOption output_opt("output", "Output folder for markdown files (default: alongside source).", "folder");
    QCommandLineOption yes_opt(QStringList{ "yes", "y" }, "Skip the folder confirmation prompt.");
    parser.addOption(source_opt);
    parser.addOption(system_opt);
    parser.addOption(temperature_opt);
    parser.addOption(output_opt);
    parser.addOption(yes_opt);

    // parse() treats the first element as the program name.
    QStringList parse_args = args;
    parse_args.prepend("ocr");

    if (!parser.parse(parse_args)) {
        std::cerr << parser.errorText().toStdString() << "\n";
        print_ocr_usage();
        return 1;
    }
    if (parser.isSet(help_opt)) {
        std::cout << parser.helpText().toStdString();
        return 0;
    }

    // --source is mandatory
    if (!parser.isSet(source_opt)) {
        std::cerr << "Error: --source is mandatory for the ocr workflow.\n";
        print_ocr_usage();
        return 1;
    }
    const QString source = parser.value(source_opt);

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
    const float temperature = static_cast<float>(temperature_result.value());

    auto endpoint_result = resolve_endpoint(ocr_model_endpoint_key, *settings);
    if (!endpoint_result.has_value()) {
        std::cerr << "Error: " << endpoint_result.error().description.toStdString() << "\n";
        return 1;
    }
    const QString endpoint = endpoint_result.value();

    auto model_name_result = resolve_model_name(ocr_model_name_key, *settings);
    if (!model_name_result.has_value()) {
        std::cerr << "Error: " << model_name_result.error().description.toStdString() << "\n";
        return 1;
    }
    const QString model_name = model_name_result.value();

    const QString output_folder = parser.value(output_opt);

    // Classify the source: file (direct, no confirmation) or folder (scan + confirm).
    QFileInfo source_info(QDir::cleanPath(source));
    QStringList sources;

    if (source_info.isDir()) {
        sources = scan_folder(source);
        if (sources.isEmpty()) {
            std::cout << "No supported sources found in " << source.toStdString() << ".\n";
            return 0;
        }

        std::cout << "Found " << sources.size() << " source(s):\n";
        for (const QString &s : sources) {
            std::cout << "  " << s.toStdString() << "\n";
        }

        // Confirm unless --yes/-y is set or stdin is not an interactive TTY.
        const bool skip_confirmation = parser.isSet(yes_opt) || !stdin_is_interactive();
        if (!skip_confirmation) {
            std::cout << "Continue? [y/N] " << std::flush;
            std::string line;
            const bool accepted = std::getline(std::cin, line) && (line == "y" || line == "Y");
            if (!accepted) {
                std::cout << "Aborted: nothing processed.\n";
                return 0;
            }
        }
    } else {
        if (!source_info.exists()) {
            std::cerr << "Error: source does not exist: " << source.toStdString() << "\n";
            return 1;
        }
        if (!has_supported_extension(source)) {
            std::cerr << "Error: unsupported file extension (expected .jpg/.jpeg/.png/.pdf): "
                      << source.toStdString() << "\n";
            return 1;
        }
        sources << source_info.absoluteFilePath();
    }

    // Process sources sequentially.
    int failures = 0;
    for (const QString &src : sources) {
        const bool is_pdf = QFileInfo(src).suffix().toLower() == "pdf";
        std::cout << "\n=== " << QFileInfo(src).fileName().toStdString() << " ===\n" << std::flush;

        const auto [success, content] = process_source(src, endpoint, temperature, system_prompt, is_pdf, model_name);
        if (!success) {
            ++failures;
            continue;
        }

        const QString target = resolve_markdown_path(src, output_folder);
        auto write_result = write_markdown_file(target, content, *settings);
        if (!write_result.has_value()) {
            std::cerr << "Error: " << write_result.error().description.toStdString() << "\n";
            ++failures;
        } else if (write_result.value().written) {
            std::cout << "Wrote " << write_result.value().target_path.toStdString() << "\n";
        } else {
            // Skipped by the general/overwrite policy: a notice, not a failure.
            std::cout << write_result.value().notice.toStdString() << "\n";
        }
    }

    if (failures > 0) {
        std::cerr << "\n" << failures << " source(s) failed.\n";
        return 1;
    }
    return 0;
}

} // namespace dir2md::cli
