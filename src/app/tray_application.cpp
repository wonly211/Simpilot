#include "tray_application.hpp"

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
#include <tlhelp32.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <stdexcept>
#include <string>

namespace simpilot {
namespace {

constexpr UINT tray_callback_message = WM_APP + 1;
constexpr UINT reload_menu_command = 2;
constexpr UINT show_everything_command = 3;
constexpr UINT exit_command = 4;
constexpr UINT english_language_command = 5;
constexpr UINT simplified_chinese_language_command = 6;
constexpr UINT repair_everything_command = 7;
constexpr UINT settings_command = 8;
constexpr UINT about_command = 10;
constexpr UINT configuration_changed_message = WM_APP + 2;
constexpr UINT open_settings_message = WM_APP + 4;
constexpr UINT_PTR everything_ready_timer = 1;
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
      settings_(AppSettingsStore::load(config_directory_ / L"Simpilot.settings.ini")),
      localization_(Localization::load(config_directory_)),
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
    window_ = CreateWindowExW(0, tray_window_class_name, localization_.text(UiText::app_title).data(), 0,
                              0, 0, 0, 0, HWND_MESSAGE, nullptr, instance_, this);
    if (!window_) throw std::runtime_error("Unable to create message window");

    add_tray_icon();
    if (!keyboard_manager_.start(window_, effective_windows_hotkey_blocking_state(settings_))) {
        logger_.write(std::format(L"Keyboard hook could not start error={}",
                                  keyboard_manager_.last_error()));
    } else {
        logger_.write(L"Keyboard hook ready");
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
    config_watcher_ = std::make_unique<ConfigWatcher>(
        config_directory_, std::vector<std::wstring>{L"Simpilot.ini", L"Simpilot2.ini"},
        [this] {
            if (window_) PostMessageW(window_, configuration_changed_message, 0, 0);
        },
        [this](const std::wstring_view message) { logger_.write(message); });
    (void)config_watcher_->start();
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
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
        (void)reload_menu();
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
    if (message == tray_callback_message && lparam == WM_LBUTTONUP) {
        show_launch_menu(1);
        return 0;
    }
    if (message == tray_callback_message
        && (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU)) {
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
            set_language(UiLanguage::english);
        } else if (identifier == simplified_chinese_language_command) {
            set_language(UiLanguage::simplified_chinese);
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
        } else if (identifier == about_command) {
            const auto about_message = localization_.language() == UiLanguage::simplified_chinese
                ? std::format(L"\u7b80\u9a6d | Simpilot\n\n\u7248\u672c {}", SIMPILOT_VERSION)
                : std::format(L"\u7b80\u9a6d | Simpilot\n\nVersion {}", SIMPILOT_VERSION);
            MessageBoxW(window, about_message.c_str(), localization_.text(UiText::about).data(),
                        MB_OK | MB_ICONINFORMATION);
        } else if (identifier == exit_command) {
            DestroyWindow(window);
        } else if (const auto found = command_entries_.find(identifier); found != command_entries_.end()) {
            execute_entry(*found->second);
        }
        return 0;
    }
    if (message == WM_DESTROY) {
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
                        instance_, nullptr, localization_.language(), executable, candidates);
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
    if (reload_in_progress_ || menu_active_ || settings_window_open_) return;
    const auto* selected_document = menu_number == 2 ? secondary_document_.get() : document_.get();
    if (!selected_document) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    command_entries_.clear();
    next_command_id_ = 1000;
    (void)MenuThemeController::apply(settings_.menu_theme);
    launch_menu_renderer_.begin(settings_.menu_theme, GetDpiForSystem());
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
    AppendMenuW(maintenance_menu, MF_SEPARATOR, 0, nullptr);
    const auto language_menu = CreatePopupMenu();
    AppendMenuW(language_menu,
                MF_STRING | (localization_.language() == UiLanguage::english ? MF_CHECKED : MF_UNCHECKED),
                english_language_command,
                localization_.text(UiText::english).data());
    AppendMenuW(language_menu,
                MF_STRING | (localization_.language() == UiLanguage::simplified_chinese
                    ? MF_CHECKED : MF_UNCHECKED),
                simplified_chinese_language_command,
                localization_.text(UiText::simplified_chinese).data());
    AppendMenuW(maintenance_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(language_menu),
                localization_.text(UiText::language).data());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(maintenance_menu),
                localization_.text(UiText::maintenance).data());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, about_command, localization_.text(UiText::about).data());
    AppendMenuW(menu, MF_STRING, exit_command, localization_.text(UiText::exit).data());
    track_menu(menu);
}

void TrayApplication::track_menu(const HMENU menu, const bool adaptive_launch_position) {
    POINT cursor;
    GetCursorPos(&cursor);
    (void)MenuThemeController::apply(settings_.menu_theme);
    SetForegroundWindow(window_);
    menu_active_ = true;
    auto alignment = static_cast<UINT>(TPM_BOTTOMALIGN | TPM_LEFTALIGN);
    if (adaptive_launch_position) {
        MONITORINFO monitor{.cbSize = sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor)) {
            alignment = launch_menu_alignment(
                cursor, monitor.rcWork, launch_menu_renderer_.measure_menu(menu));
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
    (void)MenuThemeController::apply(MenuTheme::light);
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
    unregister_global_hotkeys();
    (void)SettingsWindow::show_modal(
        instance_, nullptr, settings_, localization_.language(), keyboard_manager_,
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
        });
    settings_window_open_ = false;
    keyboard_manager_.update(effective_windows_hotkey_blocking_state(settings_));
    register_global_hotkeys();
}

bool TrayApplication::apply_settings(const AppSettings& updated) {
    const auto settings_path = config_directory_ / L"Simpilot.settings.ini";
    if (!AppSettingsStore::save(settings_path, updated)) {
        logger_.write(L"settings save failed");
        MessageBoxW(window_,
            localization_.language() == UiLanguage::simplified_chinese
                ? L"\u65e0\u6cd5\u4fdd\u5b58 Config\\Simpilot.settings.ini\u3002"
                : L"Config\\Simpilot.settings.ini could not be saved.",
            localization_.text(UiText::app_title).data(), MB_OK | MB_ICONWARNING);
        return false;
    }

    const auto menu_theme_changed = settings_.menu_theme != updated.menu_theme;
    settings_ = updated;
    if (menu_theme_changed) logger_.write(L"popup menu theme preference updated");
    keyboard_manager_.update(effective_windows_hotkey_blocking_state(settings_));
    logger_.write(L"settings saved; keyboard hook updated");
    if (!StartupRegistration::apply(settings_.start_with_windows, executable_path_)) {
        logger_.write(L"startup registration update failed");
        MessageBoxW(window_,
            localization_.language() == UiLanguage::simplified_chinese
                ? L"\u65e0\u6cd5\u66f4\u65b0\u5f53\u524d\u7528\u6237\u7684 Windows \u542f\u52a8\u9879\u3002"
                : L"The current user's Windows startup entry could not be updated.",
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
    const auto main_menu_name = localization_.language() == UiLanguage::simplified_chinese
        ? L"\u83dc\u5355 1" : L"Menu 1";
    const auto second_menu_name = localization_.language() == UiLanguage::simplified_chinese
        ? L"\u83dc\u5355 2" : L"Menu 2";
    collect(document_.get(), main_menu_name);
    collect(secondary_document_.get(), second_menu_name);
    return targets;
}

void TrayApplication::register_global_hotkeys() {
    if (settings_window_open_) return;
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
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        window_, operation, parsed->executable.c_str(),
        parsed->arguments.empty() ? nullptr : parsed->arguments.c_str(),
        config_directory_.c_str(), SW_SHOWNORMAL));
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
    const auto message = localization_.language() == UiLanguage::simplified_chinese
        ? std::format(L"无法打开：\n{}\n\n错误代码：{}", target, error)
        : std::format(L"Could not open:\n{}\n\nError code: {}", target, error);
    MessageBoxW(window_, message.c_str(), localization_.text(UiText::app_title).data(),
                MB_OK | MB_ICONERROR);
}

void TrayApplication::set_language(const UiLanguage language) {
    if (localization_.language() == language) return;
    localization_.set_language(language);
    (void)localization_.save(config_directory_);
    SetWindowTextW(window_, localization_.text(UiText::app_title).data());
    update_tray_text();
    logger_.write(language == UiLanguage::simplified_chinese
        ? L"language changed to zh-CN" : L"language changed to en-US");
}

void TrayApplication::add_tray_icon() {
    tray_icon_.cbSize = sizeof(tray_icon_);
    tray_icon_.hWnd = window_;
    tray_icon_.uID = 1;
    tray_icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray_icon_.uCallbackMessage = tray_callback_message;
    tray_icon_.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_SIMPILOT));
    wcsncpy_s(tray_icon_.szTip, localization_.text(UiText::app_title).data(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &tray_icon_);
}

void TrayApplication::update_tray_text() {
    wcsncpy_s(tray_icon_.szTip, localization_.text(UiText::app_title).data(), _TRUNCATE);
    tray_icon_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &tray_icon_);
    tray_icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

void TrayApplication::remove_tray_icon() {
    if (tray_icon_.hWnd) Shell_NotifyIconW(NIM_DELETE, &tray_icon_);
    tray_icon_.hWnd = nullptr;
}

} // namespace simpilot
