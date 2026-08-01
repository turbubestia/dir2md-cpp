
#pragma once

#include <backend/core/settings_manager.hpp>

namespace dir2md::backend {

class CoreSchema {
public:
    static void registerSchemas(SettingsManager &manager) ;
};
}