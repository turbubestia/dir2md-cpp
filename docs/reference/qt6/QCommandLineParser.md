# QCommandLineParser Reference

Qt6 built-in command-line argument parser (QtCore). No external dependencies required.

**Header:** `#include <QCommandLineParser>`  
**CMake:** `find_package(Qt6 REQUIRED COMPONENTS Core)`  
**Since:** Qt 5.x / Qt 6.x (stable API surface through 6.11)

---

## Quick Start

```cpp
#include <QCoreApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("my-tool");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("A tool for processing documents.");
    parser.addHelpOption();
    parser.addVersionOption();

    // Boolean flag: -v / --verbose
    QCommandLineOption verboseOpt({"v", "verbose"}, "Enable verbose output.");
    parser.addOption(verboseOpt);

    // Option with value: -o <file> / --output <file>
    QCommandLineOption outputOpt({"o", "output"}, "Output file path.", "file");
    parser.addOption(outputOpt);

    // Positional argument
    parser.addPositionalArgument("input", "Input file to process.");

    // Parse and handle errors (calls exit() on --help, --version, or parse error)
    parser.process(app);

    // Access parsed values
    bool verbose = parser.isSet(verboseOpt);
    QString output = parser.value(outputOpt);
    QStringList inputs = parser.positionalArguments();

    return 0;
}
```

---

## Core Methods

### Setup

| Method | Description |
|--------|-------------|
| `setApplicationDescription(desc)` | Text shown in help output before options list |
| `addHelpOption()` | Adds `-h` / `--help` (and `--help-all`). Returns `QCommandLineOption` for `isSet()` checks. Auto-handled by `process()`. |
| `addVersionOption()` | Adds `-v` / `--version`. Requires `QCoreApplication::setApplicationVersion()`. Auto-handled by `process()`. |

### Adding Options

| Method | Description |
|--------|-------------|
| `addOption(option)` | Add a single option. Returns `bool` (false on name clash or empty name). |
| `addOptions(list)` | Add multiple options at once. Returns `bool`. |
| `addPositionalArgument(name, desc, syntax?)` | Define a positional argument for help text. `syntax` is optional and appears in the Usage line. |

### Parsing

| Method | Description |
|--------|-------------|
| `process(app)` | **Recommended.** Parse args from `QCoreApplication`, handle `--help`/`--version`, and call `exit()` on error or builtin option. |
| `process(args)` | Same as above but takes a `QStringList` directly. |
| `parse(args)` | Low-level parse only. Returns `false` on error (use `errorText()` for message). Does **not** handle `--help`/`--version` or call `exit()`. Use when you need custom error handling (e.g., GUI message boxes). |

### Reading Values

| Method | Description |
|--------|-------------|
| `isSet(name)` / `isSet(option)` | `true` if the boolean flag was passed. |
| `value(name)` / `value(option)` | Last value found for an option, or default value if not specified. Returns `""` if not found. |
| `values(name)` / `values(option)` | All values found (for options used multiple times). Returns empty list if not found. |
| `positionalArguments()` | List of all non-option arguments in order. |
| `optionNames()` | Recognized option names found, in order encountered. |
| `unknownOptionNames()` | Unrecognized option names found. |

### Output & Exit

| Method | Description |
|--------|-------------|
| `helpText()` | Full help string (Usage + Options + Arguments). |
| `errorText()` | Translated error message. Call only when `parse()` returns `false`. |
| `showHelp(exitCode)` | Print help and call `exit()`. Use `0` for user-requested help, non-zero for errors. |
| `showVersion()` | Print version and call `exit(0)`. |
| `showMessageAndExit(type, msg, exitCode)` | **(since 6.9)** Print message to stdout/stderr (or Windows message box) and exit. |
| `clearPositionalArguments()` | Clear positional argument definitions. Useful for subcommand-style CLIs where you re-parse after detecting a command. |

### Parsing Modes

| Method | Description |
|--------|-------------|
| `setSingleDashWordOptionMode(mode)` | Control how `-abc` is interpreted: `ParseAsCompactedShortOptions` (default: `-a -b -c`) or `ParseAsLongOptions` (`--abc`). Must be called before `parse()`/`process()`. |
| `setOptionsAfterPositionalArgumentsMode(mode)` | Control how options after positional args are treated: `ParseAsOptions` (default) or `ParseAsPositionalArguments`. Useful for wrapper/debugger tools. Must be called before `parse()`/`process()`. |

---

## QCommandLineOption Constructor

```cpp
// Boolean flag with short name only
QCommandLineOption opt("v", "Verbose output");

// Flag with multiple names (short + long)
QCommandLineOption opt({"f", "force"}, "Overwrite existing files.");

// Option that takes a value
QCommandLineOption opt({"o", "output"}, "Output file.", "filename");

// Set default value
opt.setDefaultValue("default.txt");
```

**Value syntax on command line:**
- `--output=file` or `-o=file` (assignment)
- `--output file` or `-o file` (space-separated)
- Works even if the value starts with `-`

---

## Patterns

### Subcommand-Style CLI

Parse a positional "command" first, then reconfigure options based on the command:

```cpp
QCommandLineParser parser;
parser.addPositionalArgument("command", "The subcommand to run.");

// First parse to discover the command
parser.parse(QCoreApplication::arguments());
QString command = parser.positionalArguments().value(0);

if (command == "ocr") {
    parser.clearPositionalArguments();
    parser.addPositionalArgument("image", "Image file to process.", "[images...]");
    QCommandLineOption dpiOpt("dpi", "OCR DPI setting.", "dpi");
    parser.addOption(dpiOpt);
    parser.process(app);
    // Handle OCR command...
}
else if (command == "merge") {
    parser.clearPositionalArguments();
    parser.addPositionalArgument("files", "Files to merge.", "[files...]");
    QCommandLineOption outputOpt({"o", "output"}, "Output file.", "file");
    parser.addOption(outputOpt);
    parser.process(app);
    // Handle merge command...
}
```

### Structured Result Pattern (Complex Apps)

Return a result struct instead of calling `process()` which exits:

```cpp
struct ParseResult {
    enum class Status { Ok, Error, HelpRequested, VersionRequested };
    Status status = Status::Ok;
    std::optional<QString> error;
    // Parsed values...
    bool verbose = false;
    QString output;
    QStringList inputs;
};

ParseResult parse_args(QCommandLineParser &parser) {
    if (!parser.parse(QCoreApplication::arguments()))
        return { ParseResult::Status::Error, parser.errorText() };
    if (parser.isSet("help"))
        return { ParseResult::Status::HelpRequested };
    if (parser.isSet("version"))
        return { ParseResult::Status::VersionRequested };
    // ... populate result ...
    return { ParseResult::Status::Ok };
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    // ... setup options ...

    auto result = parse_args(parser);
    switch (result.status) {
        case ParseResult::Status::Error:
            std::fputs(qPrintable(result.error.value()), stderr);
            std::fputs("\n\n", stderr);
            std::fputs(qPrintable(parser.helpText()), stderr);
            return 1;
        case ParseResult::Status::HelpRequested:
            parser.showHelp();
            return 0;
        case ParseResult::Status::VersionRequested:
            parser.showVersion();
            return 0;
        case ParseResult::Status::Ok:
            // Use result.verbose, result.output, etc.
            break;
    }
    return 0;
}
```

---

## Known Limitations

- **Qt options are stripped early:** `QCoreApplication` parses its own options (e.g., `-reverse`, `-platform`) before `QCommandLineParser` exists. If your option value looks like a Qt option, it may be removed. Workaround: use `--` separator or avoid conflicting names.
- **No optional values:** If an option requires a value, one must always be present. An option placed last with no value is treated as if it wasn't specified.
- **No `--no-flag` negation:** The parser doesn't auto-generate negated long options. Handle explicitly by adding e.g. `{"disable-verbose"}` as an option name.

---

## Qt Version Notes

| Feature | Since |
|---------|-------|
| Core API (`addOption`, `process`, `value`, etc.) | Qt 5.x / 6.0 |
| `showMessageAndExit()` + `MessageType` enum | Qt 6.9 |

The core API is stable and identical between Qt 6.7 and Qt 6.11. The only addition in 6.9+ is the convenience `showMessageAndExit()` static method.
