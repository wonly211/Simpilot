#pragma once

#include "simpilot/app_module.hpp"
#include "simpilot/localization.hpp"
#include "simpilot/program_search_registry.hpp"
#include "simpilot/tray_menu_registry.hpp"
#include "simpilot/ui_dispatcher.hpp"
#include "simpilot/settings_registry.hpp"
#include "simpilot/hotkey_registry.hpp"
#include "simpilot/popup_menu_host.hpp"
#include "simpilot/settings_backup.hpp"

namespace simpilot {

class KeyboardManager;
std::vector<std::wstring> inspect_builtin_backup(const SettingsSnapshot&, bool check_environment = false);
void register_builtin_modules(
    ModuleRegistry& modules, HINSTANCE instance,
    const std::filesystem::path& executable_directory,
    const Localization& localization, ProgramSearchRegistry& search,
    TrayMenuRegistry& tray, UiDispatcher& dispatcher,
    SettingsRegistry& pages, SettingsParticipantRegistry& participants,
    HotkeyRegistry& hotkeys, KeyboardManager& keyboard,
    PopupMenuHost& menus, std::function<bool()> can_open,
    std::function<void()> refresh_hotkeys,
    std::function<void(std::wstring_view)> diagnose);

} // namespace simpilot
