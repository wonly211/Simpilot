#pragma once

#include "simpilot/hotkey.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace simpilot {

enum class ExistingProcessAction {
    show_window,
    start_new_instance,
    do_nothing,
};

enum class LaunchVisibility {
    normal,
    minimized,
    maximized,
    hidden,
};

enum class MenuTheme {
    system = 0,
    light = 1,
    dark = 2,
};

enum class CustomHotKeyAction {
    open_application = 0,
    open_folder = 1,
    open_file = 2,
};

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

struct BuiltInHotKey {
    HotKeyBinding binding{};
    bool enabled = false;

    bool operator==(const BuiltInHotKey&) const = default;
};

struct AppSettings {
    bool start_with_windows = false;
    MenuTheme menu_theme = MenuTheme::system;
    BuiltInHotKey main_menu{{{HotKeyGesture{0, VK_OEM_3}}, false}, true};
    BuiltInHotKey second_menu{};
    BuiltInHotKey open_settings{};
    BuiltInHotKey everything_search{
        {{HotKeyGesture{MOD_WIN, static_cast<UINT>(L'S')}}, true}, false};
    std::array<bool, 26> disabled_windows_hotkeys{};
    std::vector<CustomGlobalHotKey> custom_global_hotkeys;

    bool operator==(const AppSettings&) const = default;
};

[[nodiscard]] bool global_hotkey_requires_windows_blocking(
    const AppSettings& settings, std::size_t windows_letter_index) noexcept;

class AppSettingsStore final {
public:
    [[nodiscard]] static AppSettings load(const std::filesystem::path& path) noexcept;
    [[nodiscard]] static bool save(const std::filesystem::path& path,
                                   const AppSettings& settings) noexcept;
};

class StartupRegistration final {
public:
    [[nodiscard]] static bool apply(bool enabled,
                                    const std::filesystem::path& executable_path) noexcept;
};

} // namespace simpilot
