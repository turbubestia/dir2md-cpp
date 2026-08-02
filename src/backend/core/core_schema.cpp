
#include <backend/core/core_schema.hpp>

namespace dir2md::backend {
namespace {
    inline const QString ToolPath  = "general/core/tool_path";
    inline const QString MaxThreads = "performance/core/max_threads";
}

void CoreSchema::registerSchemas(SettingsManager &manager) {
    manager.registerSchema({
        ToolPath,
        "Tool Path",
        "Path to external execution binary.",
        "general",
        "/usr/bin/tool",
        QMetaType::fromType<QString>()
    });

    manager.registerSchema({
        MaxThreads,
        "Max Worker Threads",
        "Maximum worker threads for processing tasks.",
        "performance",
        4,
        QMetaType::fromType<int>(),
        1.0,  // min
        32.0  // max
    });
}
}