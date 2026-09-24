#include "quick_launch_module.hpp"
#include "quick_launch_settings.hpp"
#include "quick_launch_settings_ui.hpp"
#include "launch_menu_renderer.hpp"
#include "menu_theme.hpp"
#include "program_selection_dialog.hpp"
#include "program_cache.hpp"
#include "program_resolver.hpp"
#include "config_watcher.hpp"
#include "menu_parser.hpp"
#include "simpilot/command.hpp"
#include "simpilot/command_executor.hpp"
#include "simpilot/config_file.hpp"
#include "simpilot/variable_expander.hpp"
#include <shellscalingapi.h>
#include <algorithm>
#include <format>

namespace simpilot {
namespace {
void ensure_default_configuration(const std::filesystem::path& configuration_file,
                                  const Localization& localization) {
    if (std::filesystem::exists(configuration_file)) return;
    std::wstring content = L"; ";
    content.append(localization.text("ui.configuration_header"));
    content.append(L"\r\n-");
    content.append(localization.text("ui.common"));
    content.append(L"\r\n");
    content.append(localization.text("ui.notepad"));
    content.append(L"|notepad.exe\r\n");
    content.append(localization.text("ui.calculator"));
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


class QuickLaunchModule final : public IAppModule {
public:
    QuickLaunchModule(HINSTANCE instance, std::filesystem::path root,
        QuickLaunchSettings settings, const Localization& localization,
        ProgramSearchRegistry& search, TrayMenuRegistry& tray, UiDispatcher& dispatcher,
        SettingsRegistry& pages, SettingsParticipantRegistry& participants, HotkeyRegistry& hotkeys,
        PopupMenuHost& menus, std::function<bool()> can_open,
        std::function<void()> refresh_hotkeys, std::function<void(std::wstring_view)> diagnose)
        : instance_(instance), root_(std::move(root)), config_directory_(root_ / L"Config"),
          settings_(settings), draft_(settings), localization_(localization),
          program_cache_(root_ / L"Cache" / L"program-cache.tsv"),
          menu_icons_(root_ / L"Cache" / L"RunIcon"), search_registry_(search), tray_(tray),
          dispatcher_(dispatcher), pages_(pages), participants_(participants), hotkeys_(hotkeys),
          menus_(menus), can_open_(std::move(can_open)), refresh_hotkeys_(std::move(refresh_hotkeys)),
          logger_{std::move(diagnose)} {}
    ~QuickLaunchModule() override { stop(); }
    void start() override {
        if (started_) return;
        started_ = true;
        scope_ = std::make_unique<DispatchScope>();
        WNDCLASSW wc{};
        wc.hInstance = instance_;
        wc.lpfnWndProc = procedure;
        wc.lpszClassName = L"Simpilot.QuickLaunchOwner";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Cannot register launch menu owner");
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", 0,
            0, 0, 0, 0, nullptr, nullptr, instance_, this);
        if (!window_) throw std::runtime_error("Cannot create launch menu owner");
        contributions_.push_back(hotkeys_.add("quick_launch.main", 10, {
            "settings.main_menu", [this] { return settings_.main_menu; },
            [this](HWND) { show_launch_menu(1); }, [this] { return &draft_.main_menu; }}));
        contributions_.push_back(hotkeys_.add("quick_launch.second", 20, {
            "settings.second_menu", [this] {
                auto value = settings_.second_menu;
                value.enabled = value.enabled && secondary_document_ != nullptr;
                return value;
            }, [this](HWND) { show_launch_menu(2); }, [this] { return &draft_.second_menu; }}));
        contributions_.push_back(tray_.primary_actions.add("quick_launch", 10,
            [this](HWND) { show_launch_menu(1); }));
        contributions_.push_back(tray_.add("quick_launch.edit_main", 10,
            {"ui.main_menu", "ui.edit_menus", {}, [this](HWND) { open_menu_configuration(false); }}));
        contributions_.push_back(tray_.add("quick_launch.edit_second", 20,
            {"ui.second_menu", "ui.edit_menus", {}, [this](HWND) { open_menu_configuration(true); }}));
        contributions_.push_back(tray_.add("quick_launch.reload", 10,
            {"ui.reload_menu", "ui.maintenance", {}, [this](HWND) { request_reload(); }}));
        contributions_.push_back(participants_.add("quick_launch", 20, {
            .begin = [this] {
                draft_ = settings_;
                session_open_ = true;
                ui_ = make_quick_launch_settings_ui(draft_, config_directory_, root_ / L"Cache" / L"RunIcon",
                    [this] { return collect_menu_icon_targets(); },
                    [this](std::wstring_view value) { return lookup(value); },
                    [this](HWND owner, std::wstring_view value) { return reselect(owner, value); },
                    logger_.sink);
            },
            .dirty = [this] { return draft_ != settings_ || (ui_ && ui_->dirty()); },
            .validate = [this] { return ui_->validate(); },
            .prepare = [this] { previous_ = settings_; return ui_->prepare(); },
            .apply = [this] { settings_ = draft_; return ui_->apply(); },
            .write = [this](SettingsDocument& document) { draft_.write(document); },
            .rollback = [this] { settings_ = previous_; return ui_->rollback(); },
            .finish = [this] {
                menu_icons_.clear();
                (void)reload_menu();
                ui_->finish();
            },
            .cancel = [this] {
                ui_.reset();
                session_open_ = false;
                draft_ = settings_;
                if (reload_pending_) request_reload();
            }}));
        contributions_.push_back(pages_.add("quick_launch", 20, {
            "settings.tab.quick_launch", [this] { return ui_->page(false); }}));
        contributions_.push_back(pages_.add("quick_launch.icons", 30, {
            "settings.tab.menu_icons", [this] { return ui_->page(true); }}));
        (void)reload_menu();
        search_changed_ = search_registry_.on_changed("quick_launch", [this] { request_reload(); });
        config_watcher_ = std::make_unique<ConfigWatcher>(config_directory_,
            std::vector<std::wstring>{L"Simpilot.ini", L"Simpilot2.ini"},
            [this] { if (scope_) (void)dispatcher_.post(*scope_, [this] { request_reload(); }); },
            logger_.sink);
        (void)config_watcher_->start();
    }
    void stop() noexcept override {
        // Join the watcher before destroying its dispatch scope.
        config_watcher_.reset();
        scope_.reset();
        search_changed_.reset();
        contributions_.clear();
        ui_.reset();
        if (window_) DestroyWindow(window_);
        window_ = nullptr;
        started_ = false;
    }
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
        auto* module = reinterpret_cast<QuickLaunchModule*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        try {
            if (module && message == WM_MEASUREITEM
                && module->launch_menu_renderer_.measure(*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) return TRUE;
            if (module && message == WM_DRAWITEM
                && module->launch_menu_renderer_.draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE;
        } catch (...) { if (module) module->logger_.write(L"launch menu drawing failed"); }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    void request_reload() {
        if (menu_active_ || reload_in_progress_ || session_open_) { reload_pending_ = true; return; }
        reload_pending_ = false;
        (void)reload_menu(true);
    }
    void track_menu(HMENU menu, bool) {
        const auto cursor = current_cursor_position(window_);
        MONITORINFO monitor{sizeof(monitor)};
        UINT alignment = TPM_TOPALIGN | TPM_LEFTALIGN;
        if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor))
            alignment = launch_menu_alignment(cursor, monitor.rcWork, launch_menu_renderer_.measure_menu(menu));
        menu_active_ = true;
        const auto selected = menus_.track(window_, menu, alignment, cursor, [this] {
            menu_active_ = false;
            launch_menu_renderer_.end();
            (void)MenuThemeController::apply(MenuTheme::system);
        });
        if (selected) {
            const auto found = command_entries_.find(selected);
            if (found != command_entries_.end()) execute_entry(*found->second);
        }
        if (reload_pending_ && scope_) (void)dispatcher_.post(*scope_, [this] { request_reload(); });
    }
    MenuEditorWindow::ProgramResolutionInfo lookup(std::wstring_view executable) {
        MenuEditorWindow::ProgramResolutionInfo result;
        const auto value = std::wstring(executable);
        if (const auto path = ProgramResolver().resolve(value)) { result.path = *path; return result; }
        result.path = program_cache_.find(value);
        result.can_reselect = search_registry_.available();
        return result;
    }
    std::optional<std::filesystem::path> reselect(HWND owner, std::wstring_view executable) {
        const auto value = std::wstring(executable);
        const auto candidates = ProgramResolver(&search_registry_).find_candidates(value);
        if (candidates.empty()) {
            MessageBoxW(owner, localization_.text("menu_editor.reselect_failed").data(),
                localization_.text("ui.app_title").data(), MB_OK | MB_ICONWARNING);
            return {};
        }
        auto selected = std::optional<std::filesystem::path>(candidates.front().path);
        if (candidates.size() > 1) selected = ProgramSelectionDialog::show_modal(
            instance_, owner, std::string(localization_.language_code()), value, candidates);
        if (selected) { program_cache_.store(value, *selected); reload_pending_ = true; }
        return selected;
    }
    bool reload_menu(bool notify = false) noexcept;
    void show_launch_menu(int number = 1);
    void open_menu_configuration(bool secondary);
    void add_menu_children(HMENU menu, const MenuCategory& category);
    void execute_entry(const MenuEntry& entry);
    void show_launch_error(std::wstring_view target, std::uint64_t error);
    std::vector<MenuIconTarget> collect_menu_icon_targets() const;
    HINSTANCE instance_;
    std::filesystem::path root_, config_directory_;
    QuickLaunchSettings settings_, draft_, previous_;
    const Localization& localization_;
    ProgramResolutionCache program_cache_;
    MenuIconCache menu_icons_;
    LaunchMenuRenderer launch_menu_renderer_;
    ProgramSearchRegistry& search_registry_;
    TrayMenuRegistry& tray_;
    UiDispatcher& dispatcher_;
    SettingsRegistry& pages_;
    SettingsParticipantRegistry& participants_;
    HotkeyRegistry& hotkeys_;
    PopupMenuHost& menus_;
    std::function<bool()> can_open_;
    std::function<void()> refresh_hotkeys_;
    struct Diagnostics {
        std::function<void(std::wstring_view)> sink;
        void write(std::wstring_view value) const noexcept { try { if (sink) sink(value); } catch (...) {} }
    } logger_;
    HWND window_ = nullptr;
    std::unique_ptr<MenuDocument> document_, secondary_document_;
    std::unique_ptr<ConfigWatcher> config_watcher_;
    std::unique_ptr<DispatchScope> scope_;
    Registration search_changed_;
    std::vector<Registration> contributions_;
    std::shared_ptr<QuickLaunchSettingsUi> ui_;
    std::unordered_map<UINT, const MenuEntry*> command_entries_;
    UINT next_command_id_ = 1000;
    bool started_ = false, menu_active_ = false, reload_in_progress_ = false,
         reload_pending_ = false, session_open_ = false;
};
bool QuickLaunchModule::reload_menu(const bool notify_on_failure) noexcept {
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
            if (scope_) (void)dispatcher_.post(*scope_, [this] { request_reload(); });
        }
    };
    try {
        const auto configuration_file = config_directory_ / L"Simpilot.ini";
        ensure_default_configuration(configuration_file, localization_);
        auto document = std::make_unique<MenuDocument>(MenuParser::parse_file(configuration_file));
        const VariableExpander variable_expander(config_directory_.wstring());
        const MenuResolutionService resolution_service(
            ProgramResolver(&search_registry_, &program_cache_,
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
        if (refresh_hotkeys_) refresh_hotkeys_();
        finish_reload();
        return true;
    } catch (const std::exception& error) {
        logger_.write(L"menu reload failed: " + wide_error(error.what()));
    } catch (...) {
        logger_.write(L"menu reload failed: unknown error");
    }
    if (notify_on_failure) {
        MessageBoxW(window_, localization_.text("ui.reload_failed").data(),
                    localization_.text("ui.app_title").data(), MB_OK | MB_ICONWARNING);
    }
    if (refresh_hotkeys_) refresh_hotkeys_();
    finish_reload();
    return false;
}

void QuickLaunchModule::show_launch_menu(const int menu_number) {
    if (reload_in_progress_ || menu_active_ || menus_.active() || (can_open_ && !can_open_())) return;
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
                    menu, 0, localization_.text("ui.menu_two"),
                    menu_icons_.folder_icon(), secondary_menu)) {
                AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(secondary_menu),
                            localization_.text("ui.menu_two").data());
            }
        } else {
            DestroyMenu(secondary_menu);
        }
    }
    if (GetMenuItemCount(menu) == 0) {
        DestroyMenu(menu);
        launch_menu_renderer_.end();
        (void)MenuThemeController::apply(MenuTheme::system);
        MessageBeep(MB_ICONWARNING);
        return;
    }
    track_menu(menu, true);
}

void QuickLaunchModule::open_menu_configuration(const bool secondary) {
    const auto path = config_directory_
        / (secondary ? L"Simpilot2.ini" : L"Simpilot.ini");
    std::error_code error;
    auto exists = std::filesystem::exists(path, error);
    if (error) {
        logger_.write(std::format(L"menu configuration existence check failed path={} error={}"
                                  , path.wstring(), error.value()));
        MessageBoxW(window_, localization_.text("ui.open_menu_failed").data(),
                    localization_.text("ui.app_title").data(), MB_OK | MB_ICONWARNING);
        return;
    }

    if (!exists) {
        if (secondary) {
            const auto answer = MessageBoxW(
                window_, localization_.text("ui.create_menu_confirm").data(),
                localization_.text("ui.app_title").data(),
                MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION);
            if (answer != IDYES) {
                logger_.write(L"second menu configuration creation cancelled");
                return;
            }
        }

        try {
            if (secondary) {
                std::wstring template_text = L"; ";
                template_text.append(localization_.text("ui.configuration_header"));
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
            MessageBoxW(window_, localization_.text("ui.create_menu_failed").data(),
                        localization_.text("ui.app_title").data(), MB_OK | MB_ICONWARNING);
            return;
        } catch (...) {
            logger_.write(std::format(L"menu configuration creation failed path={} error=unknown",
                path.wstring()));
            MessageBoxW(window_, localization_.text("ui.create_menu_failed").data(),
                        localization_.text("ui.app_title").data(), MB_OK | MB_ICONWARNING);
            return;
        }
    }

    error.clear();
    if (!std::filesystem::is_regular_file(path, error) || error) {
        logger_.write(std::format(L"menu configuration is not a regular file path={} error={}",
                                  path.wstring(), error.value()));
        MessageBoxW(window_, localization_.text("ui.open_menu_failed").data(),
                    localization_.text("ui.app_title").data(), MB_OK | MB_ICONWARNING);
        return;
    }

    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        window_, L"open", path.c_str(), nullptr, config_directory_.c_str(), SW_SHOWNORMAL));
    if (result <= 32) {
        logger_.write(std::format(L"menu configuration open failed path={} shellError={}",
                                  path.wstring(), result));
        MessageBoxW(window_, localization_.text("ui.open_menu_failed").data(),
                    localization_.text("ui.app_title").data(), MB_OK | MB_ICONWARNING);
        return;
    }
    logger_.write(std::format(L"opened {} menu configuration path={} shellResult={}",
        secondary ? L"second" : L"main", path.wstring(), result));
}

std::vector<MenuIconTarget> QuickLaunchModule::collect_menu_icon_targets() const {
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
    const auto second_menu_name = localization_.text("ui.menu_two");
    collect(document_.get(), main_menu_name);
    collect(secondary_document_.get(), second_menu_name);
    return targets;
}

void QuickLaunchModule::add_menu_children(HMENU menu, const MenuCategory& category) {
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

void QuickLaunchModule::execute_entry(const MenuEntry& entry) {
    const auto parsed = ParsedCommand::try_parse(entry.effective_value());
    if (!parsed) return;
    execute_command(window_, LaunchRequest{
        .target = parsed->executable,
        .arguments = parsed->arguments,
        .working_directory = is_terminal_executable(parsed->executable)
            ? user_profile_directory() : config_directory_.wstring(),
        .run_as_administrator = entry.run_as_administrator},
        [this](HWND, std::wstring_view target, DWORD error) { show_launch_error(target, error); });
}

void QuickLaunchModule::show_launch_error(const std::wstring_view target,
                                        const std::uint64_t error) {
    logger_.write(std::format(L"launch failed error={} target={}", error, target));
    const auto message = format_localized(
        localization_, "ui.launch_failed", target, error);
    MessageBoxW(window_, message.c_str(), localization_.text("ui.app_title").data(),
                MB_OK | MB_ICONERROR);
}


} // namespace
std::unique_ptr<IAppModule> make_quick_launch_module(
    HINSTANCE instance, const std::filesystem::path& directory,
    const SettingsDocument& document, const Localization& localization,
    ProgramSearchRegistry& search, TrayMenuRegistry& tray, UiDispatcher& dispatcher,
    SettingsRegistry& pages, SettingsParticipantRegistry& participants, HotkeyRegistry& hotkeys,
    PopupMenuHost& menus, std::function<bool()> can_open,
    std::function<void()> refresh_hotkeys, std::function<void(std::wstring_view)> diagnose) {
    return std::make_unique<QuickLaunchModule>(instance, directory, QuickLaunchSettings::read(document),
        localization, search, tray, dispatcher, pages, participants, hotkeys, menus,
        std::move(can_open), std::move(refresh_hotkeys), std::move(diagnose));
}
} // namespace simpilot
