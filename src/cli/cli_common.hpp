#pragma once

#include <QString>
#include <QStringList>
#include <memory>
#include <optional>

#include <QObject>

#include "backend/core/expected.hpp"
#include "backend/core/settings_manager.hpp"

namespace dir2md::cli {

using expected_string = dir2md::backend::expected<QString>;
using expected_double = dir2md::backend::expected<double>;

// Result of a markdown write attempt. `written` is false when the target
// already exists and the general/overwrite policy forbids clobbering; the
// notice then explains the skip.
struct markdown_write_result {
    QString target_path;
    bool written = false;
    QString notice;
};

using expected_markdown_result = dir2md::backend::expected<markdown_write_result>;

// Bootstrap a SettingsManager shared by both workflows: registers the core
// schemas and loads persisted settings from the project-constant file name
// (resolved to ~/.config/dir2md/). A missing file is a no-op that leaves the
// schema defaults in place.
auto bootstrap_settings(QObject *parent) -> std::unique_ptr<dir2md::backend::SettingsManager>;

// Text-or-file prompt resolution: if input names an existing readable file,
// return the file's contents; otherwise return input verbatim as literal text.
auto resolve_text_or_file(const QString &input) -> expected_string;

// System-prompt fallback resolution: use the --system value (text-or-file) if
// present, else the file named by cli/system-prompt-file. Returns a clear
// hard error when the effective system prompt is missing/empty/unreadable —
// no request may be sent in that case.
auto resolve_system_prompt(const QString &system_option, dir2md::backend::SettingsManager &settings) -> expected_string;

// Temperature resolution/validation: when option_value is present (i.e.
// --temperature was given), it must parse as a real value in [0.0, 2.0];
// otherwise fall back to the cli/temperature setting.
auto resolve_temperature(std::optional<QString> option_value, dir2md::backend::SettingsManager &settings) -> expected_double;

// Endpoint resolution from settings for a given key; hard error when empty.
auto resolve_endpoint(const QString &key, dir2md::backend::SettingsManager &settings) -> expected_string;

// Resolve the markdown target path for a source: alongside the source by
// default, or inside output_folder when non-empty. The base filename is
// preserved and the extension replaced with .md.
auto resolve_markdown_path(const QString &source_path, const QString &output_folder) -> QString;

// Write markdown content to the target path, applying the general/overwrite
// policy for an existing target (false -> skip with a notice; true ->
// overwrite). Creates the parent folder if absent.
auto write_markdown_file(const QString &target_path, const QString &content, dir2md::backend::SettingsManager &settings) -> expected_markdown_result;

// Assemble PDF page content: every page (including the first) is preceded by
// a `---` line, a newline, a `**page N**` line (1-based), and a newline.
auto assemble_pdf_markdown(const QStringList &pages) -> QString;

// True when stdin is an interactive terminal (used by the confirmation
// decision: non-interactive stdin proceeds automatically).
auto stdin_is_interactive() -> bool;

} // namespace dir2md::cli
