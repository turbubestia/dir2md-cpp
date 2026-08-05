#include <QCoreApplication>
#include <QCommandLineParser>
#include <iostream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("dir2md-cli");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("CLI tool for directory-to-markdown conversion.");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption verbose_opt("verbose", "Enable verbose output.");
    parser.addOption(verbose_opt);

    parser.process(app);

    bool verbose = parser.isSet(verbose_opt);
    if (verbose) {
        std::cout << "[verbose] dir2md-cli starting..." << std::endl;
    }

    std::cout << "I am working!" << std::endl;

    if (verbose) {
        std::cout << "[verbose] dir2md-cli finished." << std::endl;
    }

    return 0;
}