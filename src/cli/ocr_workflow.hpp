#pragma once

#include <QStringList>

namespace dir2md::cli {

// Entry point for the `ocr` workflow. args is the argument list following
// the `ocr` subcommand (program name excluded). Returns a process exit code:
// 0 when every source succeeded, non-zero when any source failed or on
// argument/configuration errors.
auto execute_ocr(const QStringList &args) -> int;

} // namespace dir2md::cli
