#pragma once

#include "simpilot/app_settings.hpp"
#include "simpilot/app_module.hpp"
#include "simpilot/program_search_registry.hpp"
#include "simpilot/tray_menu_registry.hpp"
#include "simpilot/ui_dispatcher.hpp"
#include "simpilot/settings_registry.hpp"
#include "simpilot/hotkey_registry.hpp"
#include "simpilot/popup_menu_host.hpp"
#include "simpilot/logger.hpp"
#include "simpilot/localization.hpp"

#include "keyboard_manager.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>

namespace simpilot {


inline constexpr wchar_t tray_window_class_name[] = L"Simpilot.TrayWindow";
inline constexpr UINT activate_primary_message = WM_APP + 3;

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
    void show_context_menu();
    void show_settings();
    void activate_primary();
    void register_global_hotkeys();
    void unregister_global_hotkeys() noexcept;
    bool set_language(std::string language_code);
    void add_tray_icon();
    void update_tray_text();
    void remove_tray_icon();

    HINSTANCE instance_;
    std::filesystem::path executable_path_;
    std::filesystem::path config_directory_;
    Logger logger_;
    AppSettings settings_;
    AppSettings settings_draft_;
    AppSettings prepared_settings_;
    Localization localization_;
    HWND window_ = nullptr;
    NOTIFYICONDATAW tray_icon_{};
    UiDispatcher dispatcher_;
    ProgramSearchRegistry search_registry_;
    TrayMenuRegistry tray_commands_;
    SettingsRegistry settings_pages_;
    SettingsParticipantRegistry settings_participants_;
    HotkeyRegistry hotkeys_;
    std::vector<Registration> shell_hotkeys_;
    PopupMenuHost menus_;
    ModuleRegistry modules_;
    std::vector<Registration> shell_contributions_;
    std::unordered_map<UINT, std::string> contributed_tray_commands_;
    KeyboardManager keyboard_manager_;
    UINT taskbar_created_message_ = 0;
    bool settings_window_open_ = false;
};

} // namespace simpilot
