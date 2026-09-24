#include "tray_application.hpp"

#include "about_window.hpp"
#include "resource.h"
#include "settings_window.hpp"
#include "general_settings_page.hpp"
#include "hotkey_settings_page.hpp"
#include "register_builtin_modules.hpp"

#include "simpilot/localization.hpp"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj_core.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <iterator>
#include <stdexcept>
#include <string>

namespace simpilot {
namespace {

constexpr UINT tray_callback_message = WM_APP + 1;
constexpr UINT tray_icon_flags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
constexpr UINT exit_command = 4;
constexpr UINT english_language_command = 5;
constexpr UINT simplified_chinese_language_command = 6;
constexpr UINT settings_command = 8;
constexpr UINT traditional_chinese_language_command = 9;
constexpr UINT about_command = 10;
constexpr UINT open_settings_message = WM_APP + 4;
constexpr UINT_PTR keyboard_health_timer = 2;
constexpr UINT keyboard_health_interval_ms = 2000;

std::wstring process_integrity_description() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return L"unknown";
    DWORD required = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &required);
    std::vector<std::byte> buffer(required);
    if (required == 0 || !GetTokenInformation(token, TokenIntegrityLevel,
                                               buffer.data(), required, &required)) {
        CloseHandle(token);
        return L"unknown";
    }
    const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
    const auto count = *GetSidSubAuthorityCount(label->Label.Sid);
    const auto rid = *GetSidSubAuthority(label->Label.Sid, count - 1);
    TOKEN_ELEVATION elevation{};
    DWORD elevation_size = 0;
    const auto elevation_known = GetTokenInformation(
        token, TokenElevation, &elevation, sizeof(elevation), &elevation_size) != FALSE;
    CloseHandle(token);
    const wchar_t* level = rid < SECURITY_MANDATORY_MEDIUM_RID ? L"low"
        : rid < SECURITY_MANDATORY_HIGH_RID ? L"medium"
        : rid < SECURITY_MANDATORY_SYSTEM_RID ? L"high" : L"system";
    return std::format(L"{}(0x{:X}) elevated={}", level, rid,
                       elevation_known && elevation.TokenIsElevated != 0);
}

} // namespace

TrayApplication::TrayApplication(HINSTANCE instance, std::filesystem::path executable_path)
    : instance_(instance), executable_path_(std::filesystem::absolute(std::move(executable_path))),
      config_directory_(executable_path_.parent_path() / L"Config"),
      logger_(config_directory_.parent_path() / L"Log" / L"Simpilot.log"),
      settings_(AppSettingsStore::load(
          config_directory_ / L"Setting.ini",
          [this](const std::wstring_view message) { logger_.write(message); })),
      localization_(settings_.language == UiLanguage::external
          && !settings_.language_code.empty()
          ? settings_.language_code
          : std::string(Localization::language_code(settings_.language))),
      dispatcher_(instance) {
    dispatcher_.set_error_sink([this] { logger_.write(L"module UI callback failed"); });
    register_builtin_modules(
        modules_, instance_, executable_path_.parent_path(), localization_, search_registry_,
        tray_commands_, dispatcher_, settings_pages_, settings_participants_, hotkeys_, keyboard_manager_, menus_,
        [this] { return !settings_window_open_; },
        [this] { unregister_global_hotkeys(); register_global_hotkeys(); },
        [this](std::wstring_view message) { logger_.write(message); });
    shell_hotkeys_.push_back(hotkeys_.add("app.settings", 30, HotkeyContribution{
        "settings.open_settings", [this] { return settings_.open_settings; },
        [this](HWND) { show_settings(); }, [this] { return &settings_draft_.open_settings; }}));
    shell_contributions_.push_back(settings_pages_.add("general", 10, {
        "settings.tab.general", [this] { return make_general_settings_page(settings_draft_); }}));
    shell_contributions_.push_back(settings_pages_.add("hotkeys", 40, {
        "settings.tab.global_hotkeys", [this] {
            return make_hotkey_settings_page(hotkeys_, keyboard_manager_,
                [this](const auto& gesture) { return keyboard_manager_.probe_available(gesture); },
                [this](std::wstring_view value) { logger_.write(value); });
        }}));
    shell_contributions_.push_back(settings_participants_.add("app", 10, {
        .begin = [this] { settings_draft_ = settings_; },
        .dirty = [this] { return settings_draft_ != settings_; },
        .prepare = [this] { prepared_settings_ = settings_draft_; return true; },
        .apply = [this] { return StartupRegistration::apply(prepared_settings_.start_with_windows, executable_path_); },
        .write = [this](SettingsDocument& document) { AppSettingsStore::write(document, settings_draft_); },
        .rollback = [this] { return StartupRegistration::apply(settings_.start_with_windows, executable_path_); },
        .finish = [this] { settings_ = std::move(prepared_settings_); },
        .cancel = [this] { settings_draft_ = settings_; }}));
    shell_contributions_.push_back(settings_participants_.add("app.hotkeys", 10000, {
        .finish = [this] { unregister_global_hotkeys(); register_global_hotkeys(); }}));
}

TrayApplication::~TrayApplication() {
    modules_.clear();
    unregister_global_hotkeys();
    keyboard_manager_.stop();
    dispatcher_.close();
    remove_tray_icon();
    if (window_) DestroyWindow(window_);
}

int TrayApplication::run() {
    logger_.write(std::format(
        L"startup version={} executable={} config={} integrity={}",
        SIMPILOT_VERSION, executable_path_.wstring(),
        config_directory_.wstring(), process_integrity_description()));
    const WNDCLASSW window_class{
        .lpfnWndProc = &TrayApplication::window_procedure,
        .hInstance = instance_,
        .hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_SIMPILOT)),
        .lpszClassName = tray_window_class_name,
    };
    RegisterClassW(&window_class);
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW,
                              tray_window_class_name,
                              localization_.text("ui.app_title").data(), 0,
                              0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (!window_) throw std::runtime_error("Unable to create tray host window");

    taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbar_created_message_ == 0) {
        logger_.write(std::format(L"TaskbarCreated registration failed error={}",
                                  GetLastError()));
    }
    add_tray_icon();
    if (!keyboard_manager_.start(window_)) {
        logger_.write(std::format(L"Keyboard hook could not start error={}",
                                  keyboard_manager_.last_error()));
    } else {
        logger_.write(L"Keyboard hook ready");
    }
    if (SetTimer(window_, keyboard_health_timer, keyboard_health_interval_ms, nullptr) == 0) {
        logger_.write(std::format(L"Keyboard hook health timer could not start error={}",
                                  GetLastError()));
    }
    if (!StartupRegistration::apply(settings_.start_with_windows, executable_path_)) {
        logger_.write(L"startup registration synchronization failed");
    }
    modules_.start();
    register_global_hotkeys();
    MSG message{};
    while (true) {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            continue;
        }
        if (result == 0) return static_cast<int>(message.wParam);
        const auto error = GetLastError();
        logger_.write(std::format(L"message loop failed error={}", error));
        return EXIT_FAILURE;
    }
}

LRESULT CALLBACK TrayApplication::window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto* application = reinterpret_cast<TrayApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    try {
        return application ? application->handle_message(window, message, wparam, lparam)
                           : DefWindowProcW(window, message, wparam, lparam);
    } catch (...) {
        try { if (application) application->logger_.write(L"application window callback failed"); }
        catch (...) {}
        return message == WM_CREATE ? -1 : 0;
    }
}

LRESULT TrayApplication::handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (taskbar_created_message_ != 0 && message == taskbar_created_message_) {
        logger_.write(L"Windows Explorer taskbar recreated; restoring tray icon");
        add_tray_icon();
        return 0;
    }
    if (message == open_settings_message) {
        show_settings();
        return 0;
    }
    if (message == activate_primary_message) {
        activate_primary();
        return 0;
    }
    const auto tray_event = LOWORD(lparam);
    if (message == tray_callback_message && tray_event == WM_LBUTTONUP) {
        activate_primary();
        return 0;
    }
    if (message == tray_callback_message
        && (tray_event == WM_RBUTTONUP || tray_event == WM_CONTEXTMENU)) {
        show_context_menu();
        return 0;
    }
    if (message == WM_TIMER && wparam == keyboard_health_timer) {
        if (keyboard_manager_.consume_processing_diagnostic()) {
            logger_.write(
                L"keyboard processing input injection or source replay failed");
        }
        if (!keyboard_manager_.running()) {
            logger_.write(L"Keyboard hook thread stopped unexpectedly; restarting");
            unregister_global_hotkeys();
            if (keyboard_manager_.start(window_)) {
                register_global_hotkeys();
                logger_.write(L"Keyboard hook thread restarted successfully");
            } else {
                logger_.write(std::format(L"Keyboard hook restart failed error={}",
                                          keyboard_manager_.last_error()));
            }
        }
        return 0;
    }
    if (message == WM_HOTKEY) {
        (void)hotkeys_.dispatch(static_cast<int>(wparam), window_);
        return 0;
    }
    if (message == WM_COMMAND) {
        const auto identifier = LOWORD(wparam);
        if (identifier == english_language_command) {
            set_language(std::string(Localization::language_code(UiLanguage::english)));
        } else if (identifier == simplified_chinese_language_command) {
            set_language(std::string(Localization::language_code(UiLanguage::simplified_chinese)));
        } else if (identifier == traditional_chinese_language_command) {
            set_language(std::string(Localization::language_code(UiLanguage::traditional_chinese)));
        } else if (identifier == settings_command) {
            show_settings();
        } else if (identifier == about_command) {
            AboutWindow::show_modal(instance_, window, localization_, executable_path_,
                                    std::wstring(SIMPILOT_VERSION));
        } else if (identifier == exit_command) {
            PostQuitMessage(0);
        } else if (const auto command = contributed_tray_commands_.find(identifier);
                   command != contributed_tray_commands_.end()) {
            tray_commands_.visit([&](const auto& id, const TrayCommand& contribution) {
                if (id == command->second && (!contribution.enabled || contribution.enabled())) {
                    contribution.invoke(window_);
                }
            });
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_DESTROY) {
        modules_.stop();
        KillTimer(window_, keyboard_health_timer);
        remove_tray_icon();
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void TrayApplication::show_context_menu() {
    if (menus_.active() || settings_window_open_) return;
    const auto menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, settings_command, localization_.text("ui.settings").data());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const auto language_menu = CreatePopupMenu();
    AppendMenuW(language_menu, MF_STRING, simplified_chinese_language_command,
                localization_.text("ui.language.simplified_chinese").data());
    AppendMenuW(language_menu, MF_STRING, traditional_chinese_language_command,
                localization_.text("ui.language.traditional_chinese").data());
    AppendMenuW(language_menu, MF_STRING, english_language_command,
                localization_.text("ui.language.english").data());
    const auto selected_language_command =
        localization_.language() == UiLanguage::traditional_chinese
            ? traditional_chinese_language_command
            : localization_.language() == UiLanguage::english
                ? english_language_command
                : simplified_chinese_language_command;
    CheckMenuRadioItem(language_menu, english_language_command,
                       traditional_chinese_language_command, selected_language_command,
                       MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(language_menu),
                localization_.text("ui.language").data());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    std::unordered_map<std::string, HMENU> groups;
    contributed_tray_commands_.clear();
    UINT contributed_id = 40000;
    tray_commands_.visit([&](const auto& id, const TrayCommand& command) {
        if (contributed_id > 0xFFFF) return;
        auto parent = menu;
        if (!command.group.empty()) {
            auto found = groups.find(command.group);
            if (found == groups.end()) {
                const auto child = CreatePopupMenu();
                AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(child),
                    localization_.text(command.group).data());
                found = groups.emplace(command.group, child).first;
            }
            parent = found->second;
        }
        const auto flags = !command.enabled || command.enabled() ? MF_STRING : MF_GRAYED;
        AppendMenuW(parent, flags, contributed_id,
                    localization_.text(command.label_key).data());
        contributed_tray_commands_.emplace(contributed_id++, id);
    });
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, about_command, localization_.text("ui.about").data());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, exit_command, localization_.text("ui.exit").data());
    POINT point{};
    GetCursorPos(&point);
    const auto selected = menus_.track(window_, menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, point);
    if (selected) SendMessageW(window_, WM_COMMAND, selected, 0);
}

void TrayApplication::activate_primary() {
    if (settings_window_open_ || menus_.active()) return;
    bool handled = false;
    tray_commands_.primary_actions.visit([&](const auto&, const auto& invoke) {
        if (!handled) { handled = true; invoke(window_); }
    });
    if (!handled) show_settings();
}

void TrayApplication::show_settings() {
    if (settings_window_open_) return;
    if (menus_.defer_until_closed([this] { show_settings(); })) return;
    settings_window_open_ = true;
    try {
        (void)SettingsWindow::show_modal(instance_, nullptr, std::string(localization_.language_code()),
            config_directory_ / L"Setting.ini", settings_pages_, settings_participants_,
            [this](const SettingsDocument& document) {
                return document.save(config_directory_ / L"Setting.ini");
            }, [this](std::string language) { return set_language(std::move(language)); },
            [this](std::wstring_view message) { logger_.write(message); });
    } catch (...) {
        logger_.write(L"settings window could not be opened");
        MessageBoxW(window_, localization_.text("ui.settings_save_failed").data(),
            localization_.text("ui.app_title").data(), MB_OK | MB_ICONWARNING);
    }
    unregister_global_hotkeys();
    register_global_hotkeys();
    settings_window_open_ = false;
}
void TrayApplication::register_global_hotkeys() {
    unregister_global_hotkeys();
    hotkeys_.refresh(
        [this](int id, const HotKeyBinding& binding) {
            return keyboard_manager_.register_binding(id, binding);
        }, [this](std::string_view id) {
            logger_.write(L"global hotkey registration failed: " + std::wstring(id.begin(), id.end()));
        });
}

void TrayApplication::unregister_global_hotkeys() noexcept {
    keyboard_manager_.unregister_all();
    MSG queued{};
    while (window_ && PeekMessageW(&queued, window_, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {}
    hotkeys_.clear_dispatch();
}

bool TrayApplication::set_language(std::string language_code) {
    if (language_code.empty()) return false;
    if (localization_.language_code() == language_code) return true;
    bool saved = false;
    try {
        const auto path = config_directory_ / L"Setting.ini";
        auto document = SettingsDocument::load(path);
        document.set(L"General", L"Language",
                     std::wstring(language_code.begin(), language_code.end()));
        saved = document.save(path);
    } catch (...) {}
    if (!saved) {
        logger_.write(L"language preference save failed");
        MessageBoxW(window_, localization_.text("ui.settings_save_failed").data(),
                    localization_.text("ui.app_title").data(), MB_OK | MB_ICONWARNING);
        return false;
    }
    settings_.language = Localization::language_from_code(language_code);
    settings_.language_code = settings_.language == UiLanguage::external
        ? language_code : std::string{};
    settings_draft_.language = settings_.language;
    settings_draft_.language_code = settings_.language_code;
    localization_.set_language(std::move(language_code));
    SetWindowTextW(window_, localization_.text("ui.app_title").data());
    update_tray_text();
    logger_.write(L"language changed to "
        + std::wstring(localization_.language_code().begin(),
                       localization_.language_code().end()));
    return true;
}

void TrayApplication::add_tray_icon() {
    tray_icon_.cbSize = sizeof(tray_icon_);
    tray_icon_.hWnd = window_;
    tray_icon_.uID = 1;
    tray_icon_.uFlags = tray_icon_flags;
    tray_icon_.uCallbackMessage = tray_callback_message;
    tray_icon_.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_SIMPILOT));
    wcsncpy_s(tray_icon_.szTip, localization_.text("ui.app_title").data(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_ADD, &tray_icon_)) {
        logger_.write(std::format(L"tray icon registration failed error={}", GetLastError()));
        return;
    }
    tray_icon_.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &tray_icon_)) {
        logger_.write(std::format(L"tray icon version update failed error={}", GetLastError()));
    }
    tray_icon_.uFlags = tray_icon_flags;
}

void TrayApplication::update_tray_text() {
    wcsncpy_s(tray_icon_.szTip, localization_.text("ui.app_title").data(), _TRUNCATE);
    tray_icon_.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &tray_icon_);
    tray_icon_.uFlags = tray_icon_flags;
}

void TrayApplication::remove_tray_icon() {
    if (tray_icon_.hWnd) Shell_NotifyIconW(NIM_DELETE, &tray_icon_);
    tray_icon_.hWnd = nullptr;
}

} // namespace simpilot
