#pragma once

#include "simpilot/hotkey.hpp"
#include "simpilot/command_executor.hpp"
#include "simpilot/settings_document.hpp"
#include <vector>

namespace simpilot {
enum class CustomHotKeyAction { open_application = 0, open_folder = 1, open_file = 2 };
struct CustomGlobalHotKey {
    HotKeyBinding binding;
    CustomHotKeyAction action = CustomHotKeyAction::open_application;
    std::wstring program_path;
    std::wstring arguments;
    std::wstring working_directory;
    bool run_as_administrator = false;
    ExistingProcessAction existing_process_action = ExistingProcessAction::show_window;
    LaunchVisibility visibility = LaunchVisibility::normal;
    bool enabled = true;
    bool operator==(const CustomGlobalHotKey&) const = default;
};
struct CustomHotkeySettings {
    std::vector<CustomGlobalHotKey> items;
    static CustomHotkeySettings read(const SettingsDocument& document);
    void write(SettingsDocument& document) const;
    bool operator==(const CustomHotkeySettings&) const = default;
};
} // namespace simpilot
