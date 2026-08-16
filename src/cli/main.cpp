#include <QCommandLineParser>
#include <QGuiApplication>
#include <iostream>

#include "chat_workflow.hpp"
#include "ocr_workflow.hpp"

namespace {

auto print_global_usage() -> void {
    std::cerr <<
        "Usage: dir2md_cli [options] <workflow> [workflow options]\n\n"
        "Workflows:\n"
        "  chat    Chat with the language model (streams reply to stdout)\n"
        "  ocr     OCR images and PDFs into markdown files\n\n"
        "Options:\n"
        "  --verbose           Enable verbose output (applies to all workflows)\n"
        "  --help, -h          Show this help\n"
        "  --version           Show version information\n";
}

} // namespace

int main(int argc, char *argv[])
{
    // QGuiApplication (not QCoreApplication): required for QtPDF rendering in
    // the ocr workflow; harmless for chat.
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("dir2md-cli");
    QCoreApplication::setApplicationVersion("0.1.0");

    // Full argument list including the program name: parse() consumes the
    // first element as the program name itself.
    const QStringList args = QCoreApplication::arguments();

    // Global workflow parser: recognizes exactly two positional subcommands.
    QCommandLineParser global_parser;
    global_parser.setApplicationDescription("CLI tool for OCR and merging of PDFs and images.");
    const QCommandLineOption help_opt = global_parser.addHelpOption();
    const QCommandLineOption version_opt = global_parser.addVersionOption();
    global_parser.setOptionsAfterPositionalArgumentsMode(
        QCommandLineParser::ParseAsPositionalArguments);

    QCommandLineOption verbose_opt("verbose", "Enable verbose output (applies to all workflows).");
    global_parser.addOption(verbose_opt);

    if (!global_parser.parse(args)) {
        std::cerr << global_parser.errorText().toStdString() << "\n";
        print_global_usage();
        return 1;
    }
    if (global_parser.isSet(help_opt)) {
        std::cout << global_parser.helpText().toStdString();
        return 0;
    }
    if (global_parser.isSet(version_opt)) {
        std::cout << QCoreApplication::applicationName().toStdString() << " "
                  << QCoreApplication::applicationVersion().toStdString() << "\n";
        return 0;
    }

    const bool verbose = global_parser.isSet(verbose_opt);
    if (verbose) { std::cout << "[verbose] dir2md-cli starting..." << std::endl; }

    // Dispatch on the workflow subcommand.
    const QStringList positional = global_parser.positionalArguments();
    if (positional.isEmpty()) {
        std::cerr << "Error: no workflow given.\n";
        print_global_usage();
        return 1;
    }

    const QString workflow = positional.first();
    // Everything after the subcommand belongs to the workflow parser. Search
    // from index 1 so the program name (index 0) is never matched.
    const int workflow_index = args.indexOf(workflow, 1);
    const QStringList workflow_args = args.mid(workflow_index);

    int exit_code = 1;
    if (workflow == "chat") {
        exit_code = dir2md::cli::execute_chat(workflow_args);
    } else if (workflow == "ocr") {
        exit_code = dir2md::cli::execute_ocr(workflow_args);
    } else {
        std::cerr << "Error: unknown workflow '" << workflow.toStdString() << "'.\n";
        print_global_usage();
        return 1;
    }

    if (verbose) { std::cout << "[verbose] dir2md-cli finished." << std::endl; }
    return exit_code;
}