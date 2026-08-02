#pragma once

#include "simpilot/menu_model.hpp"
#include "simpilot/app_settings.hpp"
#include "simpilot/everything.hpp"
#include "simpilot/config_watcher.hpp"
#include "simpilot/logger.hpp"
#include "simpilot/localization.hpp"
#include "simpilot/program_cache.hpp"

#include "keyboard_manager.hpp"
#include "launch_menu_renderer.hpp"
#include "menu_icon_cache.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>

namespace simpilot {

struct MenuIconTarget;

inline constexpr wchar_t tray_window_class_name[] = L"Simpilot.TrayWindow";
inline constexpr UINT show_main_menu_message = WM_APP + 3;

class TrayApplication final {
public:
    TrayApplication(HINSTANCE instance, std::filesystem::path executable_path);
    ~TrayApplication();

    TrayApplication(const TrayApplication&) = delete;
    TrayApplication& operator=(const TrayApplication&) = delete;

    int run();

private:
    static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    bool reload_menu(bool notify_on_failure = false) noexcept;
    void show_launch_menu(int menu_number = 1);
    void show_context_menu();
    void track_menu(HMENU menu, bool adaptive_launch_position = false);
    void show_settings();
    [[nodiscard]] bool apply_settings(const AppSettings& settings);
    [[nodiscard]] std::vector<MenuIconTarget> collect_menu_icon_targets() const;
    void register_global_hotkeys();
    void unregister_global_hotkeys() noexcept;
    void add_menu_children(HMENU menu, const MenuCategory& category);
    void execute_entry(const MenuEntry& entry);
    void show_everything_search();
    void execute_custom_hotkey(const CustomGlobalHotKey& hotkey);
    void show_launch_error(std::wstring_view target, std::uint64_t error);
    void set_language(UiLanguage language);
    void add_tray_icon();
    void update_tray_text();
    void remove_tray_icon();

    HINSTANCE instance_;
    std::filesystem::path executable_path_;
    std::filesystem::path config_directory_;
    Logger logger_;
    ProgramResolutionCache program_cache_;
    AppSettings settings_;
    Localization localization_;
    HWND window_ = nullptr;
    NOTIFYICONDATAW tray_icon_{};
    std::unique_ptr<MenuDocument> document_;
    std::unique_ptr<MenuDocument> secondary_document_;
    std::unique_ptr<EverythingManager> everything_manager_;
    std::unique_ptr<EverythingSearch> everything_search_;
    std::unique_ptr<ConfigWatcher> config_watcher_;
    KeyboardManager keyboard_manager_;
    MenuIconCache menu_icons_;
    LaunchMenuRenderer launch_menu_renderer_;
    std::unordered_map<UINT, const MenuEntry*> command_entries_;
    std::unordered_map<int, std::size_t> custom_hotkey_entries_;
    UINT next_command_id_ = 1000;
    bool settings_window_open_ = false;
    bool settings_pending_ = false;
    bool menu_active_ = false;
    bool reload_in_progress_ = false;
    bool reload_pending_ = false;
    std::chrono::steady_clock::time_point everything_deadline_{};
};

} // namespace simpilot
