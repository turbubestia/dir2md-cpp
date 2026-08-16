
#include <backend/core/core_schema.hpp>

namespace dir2md::backend {
namespace {
    // test settings keys
    inline const QString ToolPath  = "general/core/tool_path";
    inline const QString MaxThreads = "performance/core/max_threads";

    // global settings
    inline const QString verbose = "general/verbose";
    inline const QString overwrite = "general/overwrite";
    inline const QString source_folder = "general/source-folder";
    inline const QString output_folder = "general/output-folder";

    // ocr model
    namespace ocr_model {
        inline const QString endpoint = "ocr-model/endpoint";
        inline const QString model_name = "ocr-model/model-name";
        inline const QString stream = "ocr-model/stream";
    }

    // language model
    namespace lang_model {
        inline const QString endpoint = "language-model/endpoint";
        inline const QString model_name = "language-model/model-name";
        inline const QString stream = "language-model/stream";
    }

    // cli
    namespace cli {
        inline const QString system_prompt_file = "cli/system-prompt-file";
        inline const QString temperature = "cli/temperature";
    }

    // md_gen
    namespace md_gen {
        inline const QString system_prompt_file = "markdown-generation/system-prompt-file";
        inline const QString temperature = "markdown-generation/temperature";
    }

    // md_mrg
    namespace md_mrg::merge {
        inline const QString system_prompt_file = "markdown-merging/merge/system-prompt-file";
        inline const QString temperature = "markdown-merging/merge/temperature";
    }

    namespace md_mrg::summarize {
        inline const QString system_prompt_file = "markdown-merging/summarize/system-prompt-file";
        inline const QString temperature = "markdown-merging/summarize/temperature";
    }
}

void CoreSchema::registerSchemas(SettingsManager &manager) {
    // Test setting keys only for unit tests, not for production use.
    manager.registerSchema({ ToolPath, "Tool Path", 
        "Path to external execution binary.", "General", 
        "/usr/bin/tool", QMetaType::fromType<QString>() });
    manager.registerSchema({ MaxThreads, "Max Worker Threads", 
        "Maximum worker threads for processing tasks.", "Performance",
        4, QMetaType::fromType<int>(), 1.0, 32.0 });

    // global settings
    manager.registerSchema({ verbose, "Verbose Logging", 
        "Enable verbose logging for debugging purposes.", "General",
        false, QMetaType::fromType<bool>() });
    manager.registerSchema({ overwrite, "Overwrite Existing Files", 
        "Enable overwriting of existing files.", "General",
        false, QMetaType::fromType<bool>() });
    manager.registerSchema({ source_folder, "Source Folder", 
        "Path to the source folder.", "General",
        "", QMetaType::fromType<QString>() });
    manager.registerSchema({ output_folder, "Output Folder", 
        "Path to the output folder.", "General",
        "", QMetaType::fromType<QString>() });

    // ocr model
    manager.registerSchema({ ocr_model::endpoint, "OCR Model Endpoint", 
        "Endpoint URL for the OCR model.", "OCR Model",
        "", QMetaType::fromType<QString>() });
    manager.registerSchema({ ocr_model::model_name, "OCR Model Name", 
        "Name of the OCR model to use.", "OCR Model",
        "", QMetaType::fromType<QString>() });
    manager.registerSchema({ ocr_model::stream, "OCR Model Stream", 
        "Enable streaming for the OCR model.", "OCR Model",
        false, QMetaType::fromType<bool>() });
        
    // language model
    manager.registerSchema({ lang_model::endpoint, "Language Model Endpoint", 
        "Endpoint URL for the language model.", "Language Model",
        "", QMetaType::fromType<QString>() });
    manager.registerSchema({ lang_model::model_name, "Language Model Name", 
        "Name of the language model to use.", "Language Model",
        "", QMetaType::fromType<QString>() });
    manager.registerSchema({ lang_model::stream, "Language Model Stream", 
        "Enable streaming for the language model.", "Language Model",
        false, QMetaType::fromType<bool>() });

    // cli
    manager.registerSchema({ cli::temperature, "CLI Temperature", 
        "Default sampling temperature for CLI workflows.", "CLI",
        0.7, QMetaType::fromType<double>(), 0.0, 2.0 });
    manager.registerSchema({ cli::system_prompt_file, "CLI System Prompt File", 
        "Path to the default system prompt file for CLI workflows.", "CLI",
        "", QMetaType::fromType<QString>() });

    // md_gen
    manager.registerSchema({ md_gen::system_prompt_file, "System Prompt File", 
        "Path to the system prompt file for markdown generation.", "Markdown Generation",
        "", QMetaType::fromType<QString>() });
    manager.registerSchema({ md_gen::temperature, "Temperature", "Temperature setting for markdown generation.", "Markdown Generation",
        0.7, QMetaType::fromType<double>(), 0.0, 1.5 });

    // md_mrg::merge
    manager.registerSchema({ md_mrg::merge::system_prompt_file, "Merge System Prompt File", 
        "Path to the system prompt file for markdown merging.", "Markdown Merging",
        "", QMetaType::fromType<QString>() });
    manager.registerSchema({ md_mrg::merge::temperature, "Merge Temperature", 
        "Temperature setting for markdown merging.", "Markdown Merging",
        0.7, QMetaType::fromType<double>(), 0.0, 1.5 });

    // md_mrg::summarize
    manager.registerSchema({ md_mrg::summarize::system_prompt_file, "Summarize System Prompt File", 
        "Path to the system prompt file for markdown summarization.", "Markdown Merging",
        "", QMetaType::fromType<QString>() });
    manager.registerSchema({ md_mrg::summarize::temperature, "Summarize Temperature", 
        "Temperature setting for markdown summarization.", "Markdown Merging",
        0.7, QMetaType::fromType<double>(), 0.0, 1.5 });
}
}