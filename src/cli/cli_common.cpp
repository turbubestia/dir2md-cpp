#include "cli_common.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#if defined(Q_OS_WIN)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "backend/core/core_schema.hpp"

namespace dir2md::cli {
namespace {

// Project-constant persisted settings file name (resolved by the manager to
// ~/.config/dir2md/). A missing file is a no-op on load.
inline const QString settings_file_name = "settings.json";

// Setting keys shared by both workflows.
inline const QString cli_temperature_key = "cli/temperature";
inline const QString cli_system_prompt_file_key = "cli/system-prompt-file";
inline const QString overwrite_key = "general/overwrite";

inline const double temperature_min = 0.0;
inline const double temperature_max = 2.0;

auto read_file_contents(const QString &path) -> expected_string {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return expected_string(-1, QString("Cannot read file: %1").arg(path));
    }
    QTextStream in(&file);
    return expected_string(in.readAll());
}

} // namespace

auto bootstrap_settings(QObject *parent) -> std::unique_ptr<dir2md::backend::SettingsManager> {
    auto manager = std::make_unique<dir2md::backend::SettingsManager>(parent);
    dir2md::backend::CoreSchema::registerSchemas(*manager);
    // Missing file is a no-op: schema defaults remain in place.
    manager->load_from_file(settings_file_name);
    return manager;
}

auto resolve_text_or_file(const QString &input) -> expected_string {
    const QString clean = QDir::cleanPath(input);
    QFileInfo info(clean);
    if (info.isFile() && info.isReadable()) {
        return read_file_contents(info.absoluteFilePath());
    }
    // Not an existing readable file: treat as literal text.
    return expected_string(input);
}

auto resolve_system_prompt(const QString &system_option, dir2md::backend::SettingsManager &settings) -> expected_string {
    if (!system_option.isEmpty()) {
        auto resolved = resolve_text_or_file(system_option);
        if (!resolved.has_value() || resolved.value().trimmed().isEmpty()) {
            return expected_string(-1, "System prompt is empty");
        }
        return resolved;
    }

    const QString file_path = settings.get(cli_system_prompt_file_key).toString();
    if (file_path.trimmed().isEmpty()) {
        return expected_string(-1,
                               "No system prompt: --system was not given and cli/system-prompt-file is unset");
    }

    auto resolved = resolve_text_or_file(file_path);
    if (!resolved.has_value()) {
        return expected_string(-1,
                               QString("System prompt file from settings is unreadable: %1").arg(file_path));
    }
    if (resolved.value().trimmed().isEmpty()) {
        return expected_string(-1,
                               QString("System prompt file is empty: %1").arg(file_path));
    }
    return resolved;
}

auto resolve_temperature(std::optional<QString> option_value, dir2md::backend::SettingsManager &settings) -> expected_double {
    if (option_value.has_value()) {
        bool ok = false;
        const double value = option_value->toDouble(&ok);
        if (!ok) {
            return expected_double(-1,
                                   QString("Temperature is not a number: %1").arg(*option_value));
        }
        if (value < temperature_min || value > temperature_max) {
            return expected_double(-1,
                                   QString("Temperature %1 out of range [%2, %3]")
                                       .arg(value)
                                       .arg(temperature_min)
                                       .arg(temperature_max));
        }
        return expected_double(value);
    }

    const double fallback = settings.get(cli_temperature_key).toDouble();
    if (fallback < temperature_min || fallback > temperature_max) {
        return expected_double(-1,
                               QString("cli/temperature setting out of range: %1").arg(fallback));
    }
    return expected_double(fallback);
}

auto resolve_endpoint(const QString &key, dir2md::backend::SettingsManager &settings) -> expected_string {
    const QString endpoint = settings.get(key).toString().trimmed();
    if (endpoint.isEmpty()) {
        return expected_string(-1,
                               QString("Endpoint setting %1 is empty; configure it before running").arg(key));
    }
    return expected_string(endpoint);
}

auto resolve_model_name(const QString &key, dir2md::backend::SettingsManager &settings) -> expected_string {
    const QString model_name = settings.get(key).toString().trimmed();
    if (model_name.isEmpty()) {
        return expected_string(-1,
                               QString("Model name setting %1 is empty; configure it before running").arg(key));
    }
    return expected_string(model_name);
}

auto resolve_markdown_path(const QString &source_path, const QString &output_folder) -> QString {
    QFileInfo info(QDir::cleanPath(source_path));
    const QString base_name = info.completeBaseName() + ".md";

    if (output_folder.trimmed().isEmpty()) {
        return info.dir().absoluteFilePath(base_name);
    }

    return QDir(QDir::cleanPath(output_folder)).absoluteFilePath(base_name);
}

auto write_markdown_file(const QString &target_path, const QString &content, dir2md::backend::SettingsManager &settings) -> expected_markdown_result {
    const QString clean_target = QDir::cleanPath(target_path);
    QFileInfo info(clean_target);

    if (info.exists()) {
        const bool overwrite = settings.get(overwrite_key).toBool();
        if (!overwrite) {
            markdown_write_result result;
            result.target_path = clean_target;
            result.written = false;
            result.notice = QString("Target already exists and general/overwrite is off: %1").arg(clean_target);
            return expected_markdown_result(result);
        }
    }

    QDir().mkpath(info.absolutePath());

    QFile file(clean_target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return expected_markdown_result(-1, QString("Cannot write markdown file: %1").arg(clean_target));
    }
    QTextStream out(&file);
    out << content;
    file.close();

    markdown_write_result result;
    result.target_path = clean_target;
    result.written = true;
    return expected_markdown_result(result);
}

auto assemble_pdf_markdown(const QStringList &pages) -> QString {
    QString content;
    for (int i = 0; i < pages.size(); ++i) {
        content += "---\n";
        content += "**page " + QString::number(i + 1) + "**\n";
        content += "\n";
        content += pages[i];
        if (!pages[i].endsWith('\n')) {
            content += "\n";
        }
    }
    return content;
}

auto stdin_is_interactive() -> bool {
#if defined(Q_OS_WIN)
    return _isatty(_fileno(stdin)) == 1;
#else
    return isatty(fileno(stdin)) == 1;
#endif
}

} // namespace dir2md::cli
