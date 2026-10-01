#pragma once

#include "simpilot/app_module.hpp"
#include "simpilot/settings_registry.hpp"
#include "simpilot/ui_dispatcher.hpp"

#include <filesystem>

namespace simpilot {

[[nodiscard]] std::unique_ptr<IAppModule> make_input_method_module(
    HINSTANCE instance, std::filesystem::path config_directory,
    const Localization& localization, UiDispatcher& dispatcher,
    SettingsRegistry& pages, SettingsParticipantRegistry& participants,
    std::function<void(std::wstring_view)> diagnose);

} // namespace simpilot
