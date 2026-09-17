#include "tray_application.hpp"

#include "about_window.hpp"
#include "resource.h"
#include "settings_window.hpp"
#include "menu_theme.hpp"
#include "program_selection_dialog.hpp"

#include "simpilot/command.hpp"
#include "simpilot/config_file.hpp"
#include "simpilot/everything.hpp"
#include "simpilot/localization.hpp"
#include "simpilot/menu_parser.hpp"
#include "simpilot/program_resolver.hpp"
#include "simpilot/variable_expander.hpp"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj_core.h>
#include <tlhelp32.h>

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
constexpr UINT reload_menu_command = 2;
constexpr UINT show_everything_command = 3;
constexpr UINT exit_command = 4;
constexpr UINT english_language_command = 5;
constexpr UINT simplified_chinese_language_command = 6;
constexpr UINT repair_everything_command = 7;
constexpr UINT settings_command = 8;
constexpr UINT traditional_chinese_language_command = 9;
constexpr UINT about_command = 10;
constexpr UINT edit_main_menu_command = 11;
constexpr UINT edit_second_menu_command = 12;
constexpr UINT configuration_changed_message = WM_APP + 2;
constexpr UINT open_settings_message = WM_APP + 4;
constexpr UINT_PTR everything_ready_timer = 1;
constexpr UINT_PTR keyboard_health_timer = 2;
constexpr UINT keyboard_health_interval_ms = 2000;
constexpr int main_menu_hotkey_identifier = 100;
constexpr int second_menu_hotkey_identifier = 101;
constexpr int settings_hotkey_identifier = 102;
constexpr int everything_search_hotkey_identifier = 103;
constexpr int custom_hotkey_identifier_base = 20000;

void ensure_default_configuration(const std::filesystem::path& configuration_file,
                                  const Localization& localization) {
    if (std::filesystem::exists(configuration_file)) return;
    std::wstring content = L"; ";
    content.append(localization.text(UiText::configuration_header));
    content.append(L"\r\n-");
    content.append(localization.text(UiText::common));
    content.append(L"\r\n");
    content.append(localization.text(UiText::notepad));
    content.append(L"|notepad.exe\r\n");
    content.append(localization.text(UiText::calculator));
    content.append(L"|calc.exe\r\nOpenAI|https://openai.com\r\n");
    write_configuration_text(configuration_file, content);
}

std::wstring wide_error(const char* value) {
    if (!value || !*value) return L"unknown error";
    const auto size = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (size <= 1) return L"unknown error";
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), size);
    result.pop_back();
    return result;
}

POINT current_cursor_position(const HWND fallback_window) noexcept {
    POINT cursor{};
    if (GetCursorPos(&cursor)) return cursor;

    const auto message_position = GetMessagePos();
    if (message_position != static_cast<DWORD>(-1)) {
        cursor.x = static_cast<SHORT>(LOWORD(message_position));
        cursor.y = static_cast<SHORT>(HIWORD(message_position));
        return cursor;
    }

    RECT bounds{};
    if (fallback_window && GetWindowRect(fallback_window, &bounds)) {
        cursor.x = bounds.left + (bounds.right - bounds.left) / 2;
        cursor.y = bounds.top + (bounds.bottom - bounds.top) / 2;
        return cursor;
    }

    cursor.x = GetSystemMetrics(SM_XVIRTUALSCREEN)
        + GetSystemMetrics(SM_CXVIRTUALSCREEN) / 2;
    cursor.y = GetSystemMetrics(SM_YVIRTUALSCREEN)
        + GetSystemMetrics(SM_CYVIRTUALSCREEN) / 2;
    return cursor;
}

UINT effective_dpi_for_point(const POINT point) noexcept {
    const auto monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    UINT dpi_x = 0;
    UINT dpi_y = 0;
    if (monitor && SUCCEEDED(GetDpiForMonitor(
            monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)) && dpi_x != 0) {
        return dpi_x;
    }
    return GetDpiForSystem();
}

int scale_for_dpi(const int value, const UINT dpi) noexcept {
    const auto effective_dpi = dpi == 0 ? 96u : dpi;
    return MulDiv(value, static_cast<int>(effective_dpi), 96);
}

SIZE native_menu_size(const HMENU menu, const UINT dpi) noexcept {
    SIZE result{};
    if (!menu) return result;

    const auto dc = GetDC(nullptr);
    if (!dc) return result;

    NONCLIENTMETRICSW metrics{.cbSize = sizeof(metrics)};
    HFONT font = nullptr;
    bool owns_font = false;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        font = CreateFontIndirectW(&metrics.lfMenuFont);
        owns_font = font != nullptr;
    }
    if (!font) font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const auto previous_font = font ? SelectObject(dc, font) : nullptr;
    const auto effective_dpi = dpi == 0 ? 96u : dpi;
    const auto system_item_height = GetSystemMetricsForDpi(SM_CYMENU, effective_dpi);
    const auto item_height = std::max(1, system_item_height > 0
        ? system_item_height : scale_for_dpi(GetSystemMetrics(SM_CYMENU), effective_dpi));
    const auto horizontal_padding = scale_for_dpi(32, dpi);
    const auto submenu_padding = scale_for_dpi(20, dpi);
    const auto separator_height = std::max(1, scale_for_dpi(9, dpi));

    const auto count = GetMenuItemCount(menu);
    for (int index = 0; index < count; ++index) {
        MENUITEMINFOW information{
            .cbSize = sizeof(information),
            .fMask = MIIM_FTYPE | MIIM_SUBMENU,
        };
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &information)) continue;

        if ((information.fType & MFT_SEPARATOR) != 0) {
            result.cy += separator_height;
            continue;
        }

        wchar_t text[512]{};
        const auto length = GetMenuStringW(menu, static_cast<UINT>(index), text,
                                           static_cast<int>(std::size(text)), MF_BYPOSITION);
        SIZE text_size{};
        if (length > 0 && font) {
            GetTextExtentPoint32W(dc, text, length, &text_size);
        }
        result.cx = std::max(result.cx,
                             static_cast<LONG>(text_size.cx + horizontal_padding
                                + (information.hSubMenu ? submenu_padding : 0)));
        result.cy += item_height;
    }

    if (previous_font) SelectObject(dc, previous_font);
    if (owns_font) DeleteObject(font);
    ReleaseDC(nullptr, dc);

    const auto border = std::max(1, GetSystemMetricsForDpi(SM_CXEDGE, dpi == 0 ? 96 : dpi));
    result.cx += border * 2;
    result.cy += border * 2;
    return result;
}

template <typename... Args>
std::wstring format_localized(const Localization& localization,
                              const std::string_view key, Args&&... args) {
    return std::vformat(localization.text(key),
                        std::make_wformat_args(args...));
}

std::wstring menu_label(const std::wstring_view name,
                        const std::optional<wchar_t> access_key) {
    std::wstring result;
    result.reserve(name.size() + (access_key ? 4 : 0));
    for (const auto character : name) {
        if (character == L'&') result.push_back(L'&');
        result.push_back(character);
    }
    if (access_key) {
        result.append(L"(&");
        result.push_back(*access_key);
        result.push_back(L')');
    }
    return result;
}

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

std::vector<DWORD> matching_processes(const std::filesystem::path& executable) {
    std::vector<DWORD> result;
    std::error_code error;
    const auto target = std::filesystem::absolute(executable, error).wstring();
    if (target.empty()) return result;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{.dwSize = sizeof(entry)};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                             entry.th32ProcessID);
            if (!process) continue;
            std::wstring path(32768, L'\0');
            DWORD size = static_cast<DWORD>(path.size());
            if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
                path.resize(size);
                if (_wcsicmp(path.c_str(), target.c_str()) == 0) {
                    result.push_back(entry.th32ProcessID);
                }
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

struct WindowSearch {
    const std::vector<DWORD>* processes = nullptr;
    HWND result = nullptr;
};

BOOL CALLBACK find_process_window(const HWND window, const LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (std::ranges::find(*search->processes, process) == search->processes->end()) return TRUE;
    if (GetWindow(window, GW_OWNER) != nullptr) return TRUE;
    search->result = window;
    return FALSE;
}

HWND find_process_window(const std::vector<DWORD>& processes) {
    WindowSearch search{&processes, nullptr};
    EnumWindows(&find_process_window, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

int show_command(const LaunchVisibility visibility) noexcept {
    switch (visibility) {
    case LaunchVisibility::minimized: return SW_SHOWMINIMIZED;
    case LaunchVisibility::maximized: return SW_SHOWMAXIMIZED;
    case LaunchVisibility::hidden: return SW_HIDE;
    case LaunchVisibility::normal: default: return SW_SHOWNORMAL;
    }
}

std::wstring user_profile_directory() {
    PWSTR profile = nullptr;
    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &profile))) {
        return {};
    }
    std::wstring result;
    try {
        result = profile;
    } catch (...) {
        CoTaskMemFree(profile);
        throw;
    }
    CoTaskMemFree(profile);
    return result;
}

KeyboardManager::State effective_windows_hotkey_blocking_state(
    const AppSettings& settings) noexcept {
    auto result = settings.disabled_windows_hotkeys;
    result[static_cast<std::size_t>(L'L' - L'A')] = false;
    for (std::size_t index = 0; index < result.size(); ++index) {
        if (global_hotkey_requires_windows_blocking(settings, index)) {
            result[index] = true;
        }
    }
    return result;
}

} // namespace

TrayApplication::TrayApplication(HINSTANCE instance, std::filesystem::path executable_path)
    : instance_(instance), executable_path_(std::filesystem::absolute(std::move(executable_path))),
      config_directory_(executable_path_.parent_path() / L"Config"),
      logger_(config_directory_.parent_path() / L"Log" / L"Simpilot.log"),
      program_cache_(executable_path_.parent_path() / L"Cache" / L"program-cache.tsv"),
      settings_(AppSettingsStore::load(
          config_directory_ / L"Setting.ini",
          [this](const std::wstring_view message) { logger_.write(message); })),
      localization_(settings_.language == UiLanguage::external
          && !settings_.language_code.empty()
          ? settings_.language_code
          : std::string(Localization::language_code(settings_.language))),
      cursor_locator_(instance),
      menu_icons_(executable_path_.parent_path() / L"Cache" / L"RunIcon") {}

TrayApplication::~TrayApplication() {
    config_watcher_.reset();
    unregister_global_hotkeys();
    remove_tray_icon();
    if (window_) DestroyWindow(window_);
}

int TrayApplication::run() {
    logger_.write(std::format(
        L"startup version={} executable={} config={} cacheEntries={} integrity={}",
        SIMPILOT_VERSION, executable_path_.wstring(),
        config_directory_.wstring(), program_cache_.size(), process_integrity_description()));
    const WNDCLASSW window_class{
        .lpfnWndProc = &TrayApplication::window_procedure,
        .hInstance = instance_,
        .hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_SIMPILOT)),
        .lpszClassName = tray_window_class_name,
    };
    RegisterClassW(&window_class);
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW,
                              tray_window_class_name,
                              localization_.text(UiText::app_title).data(), 0,
                              0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (!window_) throw std::runtime_error("Unable to create tray host window");

    taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbar_created_message_ == 0) {
        logger_.write(std::format(L"TaskbarCreated registration failed error={}",
                                  GetLastError()));
    }
    add_tray_icon();
    if (settings_.mouse_shake_locator_enabled) {
        if (cursor_locator_.set_enabled(true)) {
            logger_.write(L"mouse shake cursor locator enabled");
        } else {
            logger_.write(std::format(
                L"mouse shake cursor locator could not start error={}", GetLastError()));
        }
    }
    if (!keyboard_manager_.start(window_, effective_windows_hotkey_blocking_state(settings_),
                                 settings_.keyboard_mappings_enabled,
                                 settings_.keyboard_mappings)) {
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
    const auto everything_directory = config_directory_.parent_path() / L"Everything";
    everything_manager_ = std::make_unique<EverythingManager>(
        everything_directory, [this](const std::wstring_view message) { logger_.write(message); });
    everything_search_ = EverythingSearch::try_create(everything_directory / L"Everything64.dll");
    if (everything_search_) {
        if (!everything_manager_->request_start(*everything_search_)) {
            everything_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            SetTimer(window_, everything_ready_timer, 250, nullptr);
        }
    } else {
        logger_.write(L"everything SDK could not be loaded; file search disabled");
    }
    (void)reload_menu();
    const auto notification_window = window_;
    config_watcher_ = std::make_unique<ConfigWatcher>(
        config_directory_, std::vector<std::wstring>{L"Simpilot.ini", L"Simpilot2.ini"},
        [notification_window] {
            if (IsWindow(notification_window)) {
                PostMessageW(notification_window, configuration_changed_message, 0, 0);
            }
        },
        [this](const std::wstring_view message) { logger_.write(message); });
    (void)config_watcher_->start();
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
    return application ? application->handle_message(window, message, wparam, lparam)
                       : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT TrayApplication::handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (taskbar_created_message_ != 0 && message == taskbar_created_message_) {
        logger_.write(L"Windows Explorer taskbar recreated; restoring tray icon");
        add_tray_icon();
        return 0;
    }
    if (message == WM_MEASUREITEM) {
        auto* measurement = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (measurement && launch_menu_renderer_.measure(*measurement)) return TRUE;
    }
    if (message == WM_DRAWITEM) {
        const auto* drawing = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (drawing && launch_menu_renderer_.draw(*drawing)) return TRUE;
    }
    if (message == configuration_changed_message) {
        if (menu_active_) {
            reload_pending_ = true;
            logger_.write(L"configuration reload deferred until active menu closes");
            return 0;
        }
        logger_.write(L"configuration change detected; reloading menu");
        (void)reload_menu(true);
        return 0;
    }
    if (message == open_settings_message) {
        show_settings();
        return 0;
    }
    if (message == show_main_menu_message) {
        show_launch_menu(1);
        return 0;
    }
    const auto tray_event = LOWORD(lparam);
    if (message == tray_callback_message && tray_event == WM_LBUTTONUP) {
        show_launch_menu(1);
        return 0;
    }
    if (message == tray_callback_message
        && (tray_event == WM_RBUTTONUP || tray_event == WM_CONTEXTMENU)) {
        show_context_menu();
        return 0;
    }
    if (message == WM_TIMER && wparam == everything_ready_timer) {
        if (everything_search_ && everything_search_->available()) {
            KillTimer(window_, everything_ready_timer);
            logger_.write(L"everything database became ready; reloading menu");
            (void)reload_menu();
        } else if (std::chrono::steady_clock::now() >= everything_deadline_) {
            KillTimer(window_, everything_ready_timer);
            logger_.write(L"everything database readiness timed out; continuing without file search");
        }
        return 0;
    }
    if (message == WM_TIMER && wparam == keyboard_health_timer) {
        if (keyboard_manager_.consume_mapping_diagnostic()) {
            logger_.write(
                L"keyboard mapping input injection or source replay failed");
        }
        if (!keyboard_manager_.running()) {
            logger_.write(L"Keyboard hook thread stopped unexpectedly; restarting");
            unregister_global_hotkeys();
            if (keyboard_manager_.start(
                    window_, effective_windows_hotkey_blocking_state(settings_),
                    settings_.keyboard_mappings_enabled,
                    settings_.keyboard_mappings)) {
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
        const auto identifier = static_cast<int>(wparam);
        if (identifier == main_menu_hotkey_identifier) {
            show_launch_menu(1);
        } else if (identifier == second_menu_hotkey_identifier) {
            show_launch_menu(2);
        } else if (identifier == settings_hotkey_identifier) {
            show_settings();
        } else if (identifier == everything_search_hotkey_identifier) {
            show_everything_search();
        } else if (const auto custom = custom_hotkey_entries_.find(identifier);
                   custom != custom_hotkey_entries_.end()
                   && custom->second < settings_.custom_global_hotkeys.size()) {
            execute_custom_hotkey(settings_.custom_global_hotkeys[custom->second]);
        }
        return 0;
    }
    if (message == WM_COMMAND) {
        const auto identifier = LOWORD(wparam);
        if (identifier == reload_menu_command) {
            (void)reload_menu(true);
        } else if (identifier == show_everything_command) {
            show_everything_search();
        } else if (identifier == english_language_command) {
            set_language(std::string(Localization::language_code(UiLanguage::english)));
        } else if (identifier == simplified_chinese_language_command) {
            set_language(std::string(Localization::language_code(UiLanguage::simplified_chinese)));
        } else if (identifier == traditional_chinese_language_command) {
            set_language(std::string(Localization::language_code(UiLanguage::traditional_chinese)));
        } else if (identifier == repair_everything_command) {
            const auto repaired = everything_manager_ && everything_search_
                && everything_manager_->repair_service(*everything_search_, window);
            MessageBoxW(window,
                        localization_.text(repaired ? UiText::repair_everything_success
                                                    : UiText::repair_everything_failed).data(),
                        localization_.text(UiText::app_title).data(),
                        MB_OK | (repaired ? MB_ICONINFORMATION : MB_ICONWARNING));
            if (repaired) (void)reload_menu();
        } else if (identifier == settings_command) {
            show_settings();
        } else if (identifier == edit_main_menu_command) {
            open_menu_configuration(false);
        } else if (identifier == edit_second_menu_command) {
            open_menu_configuration(true);
        } else if (identifier == about_command) {
            AboutWindow::show_modal(instance_, window, localization_, executable_path_,
                                    std::wstring(SIMPILOT_VERSION));
        } else if (identifier == exit_command) {
            DestroyWindow(window);
        } else if (const auto found = command_entries_.find(identifier); found != command_entries_.end()) {
            execute_entry(*found->second);
        }
        return 0;
    }
    if (message == WM_DESTROY) {
        KillTimer(window_, everything_ready_timer);
        KillTimer(window_, keyboard_health_timer);
        config_watcher_.reset();
        remove_tray_icon();
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool TrayApplication::reload_menu(const bool notify_on_failure) noexcept {
    if (reload_in_progress_) {
        reload_pending_ = true;
        logger_.write(L"menu reload deferred until the current reload completes");
        return false;
    }
    reload_in_progress_ = true;
    const auto finish_reload = [this] {
        reload_in_progress_ = false;
        if (reload_pending_ && !menu_active_) {
            reload_pending_ = false;
            PostMessageW(window_, configuration_changed_message, 0, 0);
        }
    };
    unregister_global_hotkeys();
    try {
        const auto configuration_file = config_directory_ / L"Simpilot.ini";
        ensure_default_configuration(configuration_file, localization_);
        auto document = std::make_unique<MenuDocument>(MenuParser::parse_file(configuration_file));
        const VariableExpander variable_expander(config_directory_.wstring());
        const MenuResolutionService resolution_service(
            ProgramResolver(everything_search_.get(), &program_cache_,
                [this](const std::wstring& executable,
                       const std::vector<ProgramCandidate>& candidates) {
                    const auto selected = ProgramSelectionDialog::show_modal(
                        instance_, nullptr, std::string(localization_.language_code()),
                        executable, candidates);
                    if (selected) {
                        logger_.write(std::format(
                            L"program candidate selected executable={} path={}",
                            executable, selected->wstring()));
                    } else {
                        logger_.write(std::format(
                            L"program candidate selection cancelled executable={}", executable));
                    }
                    return selected;
                }));
        resolution_service.resolve(*document, variable_expander);

        std::unique_ptr<MenuDocument> secondary_document;
        const auto secondary_configuration_file = config_directory_ / L"Simpilot2.ini";
        if (std::filesystem::exists(secondary_configuration_file)) {
            secondary_document = std::make_unique<MenuDocument>(
                MenuParser::parse_file(secondary_configuration_file));
            resolution_service.resolve(*secondary_document, variable_expander);
        }

        const auto available = std::ranges::count_if(document->entries(),
            [](const MenuEntry* entry) { return entry->is_available; });
        const auto hidden = document->entries().size() - static_cast<std::size_t>(available);
        document_ = std::move(document);
        secondary_document_ = std::move(secondary_document);
        logger_.write(std::format(L"menu reload complete available={} hidden={} cacheEntries={}",
                                 available, hidden, program_cache_.size()));
        for (const auto* entry : document_->entries()) {
            if (!entry->is_available) {
                logger_.write(std::format(L"menu item hidden line={} name={} reason={}",
                    entry->source_line, entry->display_name,
                    entry->unavailable_reason.value_or(L"unknown")));
            }
        }
        register_global_hotkeys();
        finish_reload();
        return true;
    } catch (const std::exception& error) {
        logger_.write(L"menu reload failed: " + wide_error(error.what()));
    } catch (...) {
        logger_.write(L"menu reload failed: unknown error");
    }
    if (notify_on_failure) {
        MessageBoxW(window_, localization_.text(UiText::reload_failed).data(),
                    localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
    }
    register_global_hotkeys();
    finish_reload();
    return false;
}

void TrayApplication::show_launch_menu(const int menu_number) {
    if (reload_in_progress_ || menu_active_) return;
    const auto* selected_document = menu_number == 2 ? secondary_document_.get() : document_.get();
    if (!selected_document) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    command_entries_.clear();
    next_command_id_ = 1000;
    (void)MenuThemeController::apply(settings_.menu_theme);
    const auto cursor = current_cursor_position(window_);
    launch_menu_renderer_.begin(settings_.menu_theme, effective_dpi_for_point(cursor));
    const auto menu = CreatePopupMenu();
    add_menu_children(menu, *selected_document->root);
    if (menu_number == 1 && secondary_document_) {
        const auto secondary_menu = CreatePopupMenu();
        add_menu_children(secondary_menu, *secondary_document_->root);
        if (GetMenuItemCount(secondary_menu) > 0) {
            if (GetMenuItemCount(menu) > 0
                && !launch_menu_renderer_.append_separator(menu)) {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            }
            if (!launch_menu_renderer_.append(
                    menu, 0, localization_.text(UiText::menu_two),
                    menu_icons_.folder_icon(), secondary_menu)) {
                AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(secondary_menu),
                            localization_.text(UiText::menu_two).data());
            }
        } else {
            DestroyMenu(secondary_menu);
        }
    }
    if (GetMenuItemCount(menu) == 0) {
        DestroyMenu(menu);
        launch_menu_renderer_.end();
        MessageBeep(MB_ICONWARNING);
        return;
    }
    track_menu(menu, true);
}

void TrayApplication::show_context_menu() {
    if (reload_in_progress_ || menu_active_ || settings_window_open_) return;
    const auto menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, settings_command, localization_.text(UiText::settings).data());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const auto edit_menu = CreatePopupMenu();
    AppendMenuW(edit_menu, MF_STRING, edit_main_menu_command,
                localization_.text(UiText::main_menu).data());
    AppendMenuW(edit_menu, MF_STRING, edit_second_menu_command,
                localization_.text(UiText::second_menu).data());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit_menu),
                localization_.text(UiText::edit_menus).data());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const auto language_menu = CreatePopupMenu();
    AppendMenuW(language_menu, MF_STRING, simplified_chinese_language_command,
                localization_.text(UiText::simplified_chinese).data());
    AppendMenuW(language_menu, MF_STRING, traditional_chinese_language_command,
                localization_.text(UiText::traditional_chinese).data());
    AppendMenuW(language_menu, MF_STRING, english_language_command,
                localization_.text(UiText::english).data());
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
                localization_.text(UiText::language).data());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const auto maintenance_menu = CreatePopupMenu();
    AppendMenuW(maintenance_menu, MF_STRING, reload_menu_command,
                localization_.text(UiText::reload_menu).data());
    AppendMenuW(maintenance_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(maintenance_menu,
                everything_manager_ && everything_manager_->components_available() ? MF_STRING : MF_GRAYED,
                show_everything_command,
                localization_.text(UiText::open_everything).data());
    AppendMenuW(maintenance_menu,
                everything_manager_ && everything_search_
                    && everything_manager_->components_available()
                    ? MF_STRING : MF_GRAYED,
                repair_everything_command,
                localization_.text(UiText::repair_everything).data());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(maintenance_menu),
                localization_.text(UiText::maintenance).data());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, about_command, localization_.text(UiText::about).data());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, exit_command, localization_.text(UiText::exit).data());
    track_menu(menu, true);
}

void TrayApplication::open_menu_configuration(const bool secondary) {
    const auto path = config_directory_
        / (secondary ? L"Simpilot2.ini" : L"Simpilot.ini");
    std::error_code error;
    auto exists = std::filesystem::exists(path, error);
    if (error) {
        logger_.write(std::format(L"menu configuration existence check failed path={} error={}"
                                  , path.wstring(), error.value()));
        MessageBoxW(window_, localization_.text(UiText::open_menu_failed).data(),
                    localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
        return;
    }

    if (!exists) {
        if (secondary) {
            const auto answer = MessageBoxW(
                window_, localization_.text(UiText::create_menu_confirm).data(),
                localization_.text(UiText::app_title).data(),
                MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION);
            if (answer != IDYES) {
                logger_.write(L"second menu configuration creation cancelled");
                return;
            }
        }

        try {
            if (secondary) {
                std::wstring template_text = L"; ";
                template_text.append(localization_.text(UiText::configuration_header));
                template_text.append(L"\r\n");
                write_configuration_text(path, template_text);
            } else {
                ensure_default_configuration(path, localization_);
            }
            exists = true;
            logger_.write(std::format(L"created {} menu configuration path={}",
                secondary ? L"second" : L"main", path.wstring()));
        } catch (const std::exception& exception) {
            logger_.write(std::format(L"menu configuration creation failed path={} error={}",
                path.wstring(), wide_error(exception.what())));
            MessageBoxW(window_, localization_.text(UiText::create_menu_failed).data(),
                        localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
            return;
        } catch (...) {
            logger_.write(std::format(L"menu configuration creation failed path={} error=unknown",
                path.wstring()));
            MessageBoxW(window_, localization_.text(UiText::create_menu_failed).data(),
                        localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
            return;
        }
    }

    error.clear();
    if (!std::filesystem::is_regular_file(path, error) || error) {
        logger_.write(std::format(L"menu configuration is not a regular file path={} error={}",
                                  path.wstring(), error.value()));
        MessageBoxW(window_, localization_.text(UiText::open_menu_failed).data(),
                    localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
        return;
    }

    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        window_, L"open", path.c_str(), nullptr, config_directory_.c_str(), SW_SHOWNORMAL));
    if (result <= 32) {
        logger_.write(std::format(L"menu configuration open failed path={} shellError={}",
                                  path.wstring(), result));
        MessageBoxW(window_, localization_.text(UiText::open_menu_failed).data(),
                    localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
        return;
    }
    logger_.write(std::format(L"opened {} menu configuration path={} shellResult={}",
        secondary ? L"second" : L"main", path.wstring(), result));
}

void TrayApplication::track_menu(const HMENU menu, const bool adaptive_launch_position) {
    const auto cursor = current_cursor_position(window_);
    (void)MenuThemeController::apply(settings_.menu_theme);
    SetForegroundWindow(window_);
    menu_active_ = true;
    auto alignment = static_cast<UINT>(TPM_BOTTOMALIGN | TPM_LEFTALIGN);
    if (adaptive_launch_position) {
        MONITORINFO monitor{.cbSize = sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor)) {
            auto menu_size = launch_menu_renderer_.measure_menu(menu);
            if (menu_size.cx <= 0 || menu_size.cy <= 0) {
                menu_size = native_menu_size(menu, effective_dpi_for_point(cursor));
            }
            alignment = launch_menu_alignment(
                cursor, monitor.rcWork, menu_size);
        } else {
            alignment = TPM_TOPALIGN | TPM_LEFTALIGN;
        }
    }
    const auto selected = TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | alignment | TPM_RETURNCMD | TPM_NONOTIFY,
        cursor.x, cursor.y, 0, window_, nullptr);
    menu_active_ = false;
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    launch_menu_renderer_.end();
    (void)MenuThemeController::apply(MenuTheme::system);
    const auto open_settings_after_menu = settings_pending_;
    settings_pending_ = false;
    if (selected != 0 && !open_settings_after_menu) {
        SendMessageW(window_, WM_COMMAND, selected, 0);
    }
    if (reload_pending_) {
        reload_pending_ = false;
        PostMessageW(window_, configuration_changed_message, 0, 0);
    }
    if (open_settings_after_menu) {
        PostMessageW(window_, open_settings_message, 0, 0);
    }
}

void TrayApplication::show_settings() {
    if (reload_in_progress_) return;
    if (menu_active_) {
        settings_pending_ = true;
        EndMenu();
        return;
    }
    if (settings_window_open_) return;
    (void)MenuThemeController::apply(MenuTheme::light);
    settings_window_open_ = true;
    (void)SettingsWindow::show_modal(
        instance_, nullptr, settings_, keyboard_manager_,
        [this](const HotKeyGesture& gesture) {
            return keyboard_manager_.probe_available(gesture);
        },
        [this](const std::wstring_view message) { logger_.write(message); },
        collect_menu_icon_targets(),
        config_directory_,
        executable_path_.parent_path() / L"Cache" / L"RunIcon",
        [this] { menu_icons_.clear(); },
        [this](const AppSettings& updated) { return apply_settings(updated); },
        [this] {
            menu_icons_.clear();
            (void)reload_menu(true);
            return collect_menu_icon_targets();
        },
        [this](const UiLanguage, const std::string_view language_code) {
            set_language(std::string(language_code));
        },
        [this](const std::wstring_view executable) {
            MenuEditorWindow::ProgramResolutionInfo result;
            const auto value = std::wstring(executable);
            if (const auto system_path = ProgramResolver().resolve(value)) {
                result.path = *system_path;
                return result;
            }
            result.path = program_cache_.find(value);
            result.can_reselect = everything_search_ != nullptr;
            return result;
        },
        [this](const HWND owner, const std::wstring_view executable)
            -> std::optional<std::filesystem::path> {
            const auto value = std::wstring(executable);
            const auto candidates = ProgramResolver(everything_search_.get())
                .find_candidates(value);
            if (candidates.empty()) {
                MessageBoxW(owner,
                    localization_.text("menu_editor.reselect_failed").data(),
                    localization_.text(UiText::app_title).data(),
                    MB_OK | MB_ICONWARNING);
                return std::nullopt;
            }
            auto selected = candidates.front().path;
            if (candidates.size() > 1) {
                const auto requested = ProgramSelectionDialog::show_modal(
                    instance_, owner, std::string(localization_.language_code()), value, candidates);
                if (!requested) return std::nullopt;
                selected = *requested;
            }
            program_cache_.store(value, selected);
            logger_.write(std::format(L"program resolution changed executable={} path={}",
                                      value, selected.wstring()));
            (void)reload_menu(true);
            return selected;
        });
    settings_window_open_ = false;
}

bool TrayApplication::apply_settings(const AppSettings& updated) {
    const auto settings_path = config_directory_ / L"Setting.ini";
    const auto mapping_errors = validate_keyboard_mappings(updated.keyboard_mappings);
    if (!mapping_errors.empty()) {
        logger_.write(std::format(L"keyboard mapping validation failed count={}",
                                  mapping_errors.size()));
        MessageBoxW(window_,
            localization_.text("settings.keyboard_mappings.invalid").data(),
            localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
        return false;
    }
    const auto previous_settings = settings_;
    const auto previous_cursor_locator_state = cursor_locator_.enabled();
    const auto cursor_locator_changed = previous_cursor_locator_state
        != updated.mouse_shake_locator_enabled;
    if (cursor_locator_changed
        && !cursor_locator_.set_enabled(updated.mouse_shake_locator_enabled)) {
        logger_.write(std::format(
            L"mouse shake cursor locator update failed error={}", GetLastError()));
        MessageBoxW(window_,
            localization_.text("ui.cursor_locator_update_failed").data(),
            localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!keyboard_manager_.update_mappings(updated.keyboard_mappings_enabled,
                                           updated.keyboard_mappings)) {
        logger_.write(std::format(L"keyboard mapping runtime update failed error={}",
                                  keyboard_manager_.last_error()));
        if (cursor_locator_changed
            && !cursor_locator_.set_enabled(previous_cursor_locator_state)) {
            logger_.write(L"mouse shake cursor locator rollback failed");
        }
        MessageBoxW(window_,
            localization_.text("settings.keyboard_mappings.update_failed").data(),
            localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!AppSettingsStore::save(settings_path, updated)) {
        (void)keyboard_manager_.update_mappings(
            previous_settings.keyboard_mappings_enabled,
            previous_settings.keyboard_mappings);
        if (cursor_locator_changed
            && !cursor_locator_.set_enabled(previous_cursor_locator_state)) {
            logger_.write(L"mouse shake cursor locator rollback failed");
        }
        logger_.write(L"settings save failed");
        MessageBoxW(window_,
            localization_.text("ui.settings_save_failed").data(),
            localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
        return false;
    }

    unregister_global_hotkeys();
    const auto menu_theme_changed = settings_.menu_theme != updated.menu_theme;
    settings_ = updated;
    if (menu_theme_changed) logger_.write(L"popup menu theme preference updated");
    if (cursor_locator_changed) {
        logger_.write(settings_.mouse_shake_locator_enabled
            ? L"mouse shake cursor locator enabled"
            : L"mouse shake cursor locator disabled");
    }
    keyboard_manager_.update(effective_windows_hotkey_blocking_state(settings_));
    register_global_hotkeys();
    logger_.write(L"settings saved; keyboard hook updated");
    if (!StartupRegistration::apply(settings_.start_with_windows, executable_path_)) {
        logger_.write(L"startup registration update failed");
        MessageBoxW(window_,
            localization_.text("ui.startup_update_failed").data(),
            localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
    }
    return true;
}

std::vector<MenuIconTarget> TrayApplication::collect_menu_icon_targets() const {
    std::vector<MenuIconTarget> targets;
    const auto collect = [this, &targets](MenuDocument* document,
                                          const std::wstring_view menu_name) {
        if (!document) return;
        for (const auto* entry : document->entries()) {
            if (!entry || !entry->is_available || entry->kind != MenuEntryKind::command) continue;
            const auto icon_source = MenuIconCache::target_for(*entry);
            std::error_code error;
            if (!icon_source || !std::filesystem::is_regular_file(*icon_source, error)) continue;
            const auto custom_key = MenuIconCache::custom_key_for(*entry);
            const auto existing = std::ranges::find_if(targets,
                [&custom_key](const MenuIconTarget& item) {
                    return item.custom_key == custom_key;
                });
            if (existing == targets.end()) {
                targets.push_back({std::wstring(menu_name), entry->display_name,
                    entry->effective_value(), custom_key, *icon_source, entry->kind});
            } else if (existing->menu_name != menu_name) {
                existing->menu_name.append(L" / ").append(menu_name);
            }
        }
    };
    const auto main_menu_name = localization_.text("ui.menu_one");
    const auto second_menu_name = localization_.text(UiText::menu_two);
    collect(document_.get(), main_menu_name);
    collect(secondary_document_.get(), second_menu_name);
    return targets;
}

void TrayApplication::register_global_hotkeys() {
    const auto register_binding = [this](const int identifier, const HotKeyBinding& binding,
                                         const std::wstring_view name) {
        if (binding.gesture && !keyboard_manager_.register_binding(identifier, binding)) {
            logger_.write(std::format(L"global hotkey registration failed name={} gesture={}",
                                     name, binding.gesture->display_text()));
        }
    };
    if (settings_.main_menu.enabled) {
        register_binding(main_menu_hotkey_identifier, settings_.main_menu.binding, L"main-menu");
    }
    if (secondary_document_ && settings_.second_menu.enabled) {
        register_binding(second_menu_hotkey_identifier, settings_.second_menu.binding,
                         L"second-menu");
    }
    if (settings_.open_settings.enabled) {
        register_binding(settings_hotkey_identifier, settings_.open_settings.binding,
                         L"settings");
    }
    if (settings_.everything_search.enabled
        && settings_.everything_search.binding.gesture) {
        auto binding = settings_.everything_search.binding;
        if (is_supported_windows_letter_hotkey(*binding.gesture)) {
            binding.force_override = true;
        }
        register_binding(everything_search_hotkey_identifier, binding,
                         L"everything-search");
    }

    custom_hotkey_entries_.clear();
    for (std::size_t index = 0; index < settings_.custom_global_hotkeys.size(); ++index) {
        const auto& hotkey = settings_.custom_global_hotkeys[index];
        if (!hotkey.enabled || !hotkey.binding.gesture) continue;
        if (const auto windows_index = windows_letter_hotkey_index(*hotkey.binding.gesture);
            windows_index && *windows_index == static_cast<std::size_t>(L'L' - L'A')) {
            logger_.write(L"custom global hotkey ignored because Win+L cannot be overridden");
            continue;
        }
        auto binding = hotkey.binding;
        if (is_supported_windows_letter_hotkey(*binding.gesture)) {
            binding.force_override = true;
        }
        const auto identifier = custom_hotkey_identifier_base + static_cast<int>(index);
        if (keyboard_manager_.register_binding(identifier, binding)) {
            custom_hotkey_entries_.emplace(identifier, index);
        } else {
            logger_.write(std::format(L"custom global hotkey registration failed gesture={} program={}",
                binding.gesture->display_text(), hotkey.program_path));
        }
    }

}

void TrayApplication::unregister_global_hotkeys() noexcept {
    keyboard_manager_.unregister_all();
    custom_hotkey_entries_.clear();
}

void TrayApplication::add_menu_children(HMENU menu, const MenuCategory& category) {
    for (const auto& child : category.children) {
        if (const auto* separator = dynamic_cast<const MenuSeparator*>(child.get())) {
            if (!launch_menu_renderer_.append_separator(menu)) {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            }
            continue;
        }
        if (const auto* nested = dynamic_cast<const MenuCategory*>(child.get())) {
            const auto submenu = CreatePopupMenu();
            add_menu_children(submenu, *nested);
            if (GetMenuItemCount(submenu) == 0) {
                DestroyMenu(submenu);
            } else {
                const auto label = menu_label(nested->name, nested->access_key);
                if (!launch_menu_renderer_.append(
                        menu, 0, label, menu_icons_.folder_icon(), submenu)) {
                    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu),
                                label.c_str());
                }
            }
            continue;
        }
        const auto* entry = dynamic_cast<const MenuEntry*>(child.get());
        if (!entry || !entry->is_available) continue;
        const auto command_id = next_command_id_++;
        command_entries_.emplace(command_id, entry);
        const auto label = menu_label(entry->display_name, entry->access_key);
        if (!launch_menu_renderer_.append(
                menu, command_id, label, menu_icons_.icon_for(*entry))) {
            AppendMenuW(menu, MF_STRING, command_id, label.c_str());
        }
    }
}

void TrayApplication::execute_entry(const MenuEntry& entry) {
    const auto parsed = ParsedCommand::try_parse(entry.effective_value());
    if (!parsed) return;
    const auto operation = entry.run_as_administrator ? L"runas" : L"open";
    const auto working_directory = is_terminal_executable(parsed->executable)
        ? user_profile_directory() : config_directory_.wstring();
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        window_, operation, parsed->executable.c_str(),
        parsed->arguments.empty() ? nullptr : parsed->arguments.c_str(),
        working_directory.empty() ? nullptr : working_directory.c_str(),
        SW_SHOWNORMAL));
    if (result <= 32) show_launch_error(parsed->executable, static_cast<std::uint64_t>(result));
}

void TrayApplication::show_everything_search() {
    logger_.write(L"opening Everything search window through the built-in action");
    if (everything_manager_ && everything_manager_->show_window(window_)) return;
    MessageBoxW(window_, localization_.text(UiText::everything_unavailable).data(),
                localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
}

void TrayApplication::execute_custom_hotkey(const CustomGlobalHotKey& hotkey) {
    const VariableExpander expander(config_directory_.wstring());
    const auto expand_path = [this, &expander](const std::wstring_view value) {
        auto path = std::filesystem::path(expander.expand(std::wstring(value)));
        if (path.is_relative()) path = config_directory_ / path;
        return path.lexically_normal().wstring();
    };
    const auto program_path = expand_path(hotkey.program_path);
    const auto working_directory = hotkey.working_directory.empty()
        ? std::wstring{}
        : expand_path(hotkey.working_directory);
    const auto application = hotkey.action == CustomHotKeyAction::open_application;
    if (application) {
        const auto processes = matching_processes(program_path);
        if (!processes.empty()) {
            if (hotkey.existing_process_action == ExistingProcessAction::do_nothing) return;
            if (hotkey.existing_process_action == ExistingProcessAction::show_window) {
                if (const auto target = find_process_window(processes)) {
                    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                    else ShowWindow(target, SW_SHOW);
                    BringWindowToTop(target);
                    SetForegroundWindow(target);
                    return;
                }
            }
        }
    }

    const auto operation = application && hotkey.run_as_administrator ? L"runas" : L"open";
    const auto directory = !application || working_directory.empty()
        ? nullptr : working_directory.c_str();
    SHELLEXECUTEINFOW execution{
        .cbSize = sizeof(execution),
        .fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI,
        .hwnd = window_,
        .lpVerb = operation,
        .lpFile = program_path.c_str(),
        .lpParameters = !application || hotkey.arguments.empty() ? nullptr : hotkey.arguments.c_str(),
        .lpDirectory = directory,
        .nShow = application ? show_command(hotkey.visibility) : SW_SHOWNORMAL,
    };
    if (!ShellExecuteExW(&execution)) {
        show_launch_error(program_path, GetLastError());
        return;
    }
    if (execution.hProcess) CloseHandle(execution.hProcess);
}

void TrayApplication::show_launch_error(const std::wstring_view target,
                                        const std::uint64_t error) {
    logger_.write(std::format(L"launch failed error={} target={}", error, target));
    const auto message = format_localized(
        localization_, "ui.launch_failed", target, error);
    MessageBoxW(window_, message.c_str(), localization_.text(UiText::app_title).data(),
                MB_OK | MB_ICONERROR);
}

void TrayApplication::set_language(std::string language_code) {
    if (language_code.empty() || localization_.language_code() == language_code) return;
    settings_.language = Localization::language_from_code(language_code);
    settings_.language_code = settings_.language == UiLanguage::external
        ? language_code : std::string{};
    localization_.set_language(std::move(language_code));
    if (!AppSettingsStore::save(config_directory_ / L"Setting.ini", settings_)) {
        logger_.write(L"language preference save failed");
    }
    SetWindowTextW(window_, localization_.text(UiText::app_title).data());
    update_tray_text();
    logger_.write(L"language changed to "
        + std::wstring(localization_.language_code().begin(),
                       localization_.language_code().end()));
}

void TrayApplication::add_tray_icon() {
    tray_icon_.cbSize = sizeof(tray_icon_);
    tray_icon_.hWnd = window_;
    tray_icon_.uID = 1;
    tray_icon_.uFlags = tray_icon_flags;
    tray_icon_.uCallbackMessage = tray_callback_message;
    tray_icon_.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_SIMPILOT));
    wcsncpy_s(tray_icon_.szTip, localization_.text(UiText::app_title).data(), _TRUNCATE);
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
    wcsncpy_s(tray_icon_.szTip, localization_.text(UiText::app_title).data(), _TRUNCATE);
    tray_icon_.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &tray_icon_);
    tray_icon_.uFlags = tray_icon_flags;
}

void TrayApplication::remove_tray_icon() {
    if (tray_icon_.hWnd) Shell_NotifyIconW(NIM_DELETE, &tray_icon_);
    tray_icon_.hWnd = nullptr;
}

} // namespace simpilot
