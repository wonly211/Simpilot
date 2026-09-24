#pragma once
#include "simpilot/app_module.hpp"
#include "simpilot/settings_registry.hpp"
#include "simpilot/hotkey_registry.hpp"
#include "simpilot/program_search_registry.hpp"
#include "simpilot/tray_menu_registry.hpp"
#include "simpilot/ui_dispatcher.hpp"
#include "simpilot/popup_menu_host.hpp"

namespace simpilot {
std::unique_ptr<IAppModule> make_quick_launch_module(
    HINSTANCE instance, const std::filesystem::path& executable_directory,
    const SettingsDocument& document, const Localization& localization,
    ProgramSearchRegistry& search, TrayMenuRegistry& tray, UiDispatcher& dispatcher,
    SettingsRegistry& pages, SettingsParticipantRegistry& participants, HotkeyRegistry& hotkeys,
    PopupMenuHost& menus, std::function<bool()> can_open,
    std::function<void()> refresh_hotkeys, std::function<void(std::wstring_view)> diagnose);
} // namespace simpilot
