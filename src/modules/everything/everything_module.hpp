#pragma once

#include "simpilot/app_module.hpp"
#include "simpilot/settings_registry.hpp"
#include "simpilot/hotkey_registry.hpp"

#include <filesystem>
#include <functional>

namespace simpilot {

class Localization;
class ProgramSearchRegistry;
class UiDispatcher;
class TrayMenuRegistry;

[[nodiscard]] std::unique_ptr<IAppModule> make_everything_module(
    std::filesystem::path directory, const Localization& localization,
    ProgramSearchRegistry& search, TrayMenuRegistry& tray,
    UiDispatcher& dispatcher, HotkeyRegistry& hotkeys,
    SettingsRegistry& pages, SettingsParticipantRegistry& participants,
    const SettingsDocument& document,
    std::function<void(std::wstring_view)> diagnose);

} // namespace simpilot
