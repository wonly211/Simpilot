#pragma once
#include "simpilot/settings_backup.hpp"
#include "simpilot/app_module.hpp"
#include "simpilot/hotkey_registry.hpp"
#include "simpilot/command_executor.hpp"

namespace simpilot {
void inspect_custom_hotkey_backup(const SettingsSnapshot&, bool, std::vector<std::wstring>&);
class KeyboardManager;
std::unique_ptr<IAppModule> make_custom_hotkey_module(
    const SettingsDocument& document, const std::filesystem::path& config_directory,
    SettingsParticipantRegistry& participants, HotkeyRegistry& hotkeys,
    KeyboardManager& keyboard, LaunchErrorSink launch_error,
    std::function<void(std::wstring_view)> diagnose);
} // namespace simpilot
