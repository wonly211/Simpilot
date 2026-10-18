#pragma once
#include "simpilot/settings_backup.hpp"

#include "simpilot/app_module.hpp"
#include "simpilot/settings_registry.hpp"
#include "simpilot/ui_dispatcher.hpp"

#include <filesystem>

namespace simpilot {
void inspect_input_method_backup(const SettingsSnapshot&, bool, std::vector<std::wstring>&);

[[nodiscard]] std::unique_ptr<IAppModule> make_input_method_module(
    HINSTANCE instance, std::filesystem::path config_directory,
    const Localization& localization, UiDispatcher& dispatcher,
    SettingsRegistry& pages, SettingsParticipantRegistry& participants,
    std::function<void(std::wstring_view)> diagnose);

} // namespace simpilot
