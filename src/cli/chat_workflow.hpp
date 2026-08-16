#pragma once

#include <QStringList>

namespace dir2md::cli {

// Entry point for the `chat` workflow. args is the argument list following
// the `chat` subcommand (program name excluded). Returns a process exit code:
// 0 on success, non-zero on any error. Produces no output file; the model
// reply streams to stdout.
auto execute_chat(const QStringList &args) -> int;

} // namespace dir2md::cli
