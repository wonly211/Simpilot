#pragma once
#include "simpilot/app_settings.hpp"
#include "simpilot/settings_registry.hpp"
namespace simpilot {
std::unique_ptr<ISettingsPage> make_general_settings_page(AppSettings& draft);
}
