#include "everything_module.hpp"

#include "everything.hpp"
#include "simpilot/localization.hpp"
#include "simpilot/program_search_registry.hpp"
#include "simpilot/tray_menu_registry.hpp"
#include "simpilot/ui_dispatcher.hpp"

#include <cwctype>
#include <stdexcept>

namespace simpilot {
namespace {

class EverythingSettingsPage final : public ISettingsPage {
public:
    EverythingSettingsPage(EverythingManager& manager, const EverythingSearch* search,
                           std::function<void(HWND)> open, std::function<void(HWND)> repair)
        : manager_(manager), search_(search), open_(std::move(open)), repair_(std::move(repair)) {}
    ~EverythingSettingsPage() override { if (window_) DestroyWindow(window_); }
    void create(const SettingsPageContext& context) override {
        WNDCLASSW window_class{
            .lpfnWndProc = &EverythingSettingsPage::procedure,
            .hInstance = context.instance,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
            .lpszClassName = L"Simpilot.EverythingSettingsPage",
        };
        if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Cannot register Everything settings page");
        }
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, window_class.lpszClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, context.parent, nullptr, context.instance, this);
        if (!window_) throw std::runtime_error("Cannot create Everything settings page");
        heading_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window_, nullptr, context.instance, nullptr);
        open_button_ = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(1), context.instance, nullptr);
        repair_button_ = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(2), context.instance, nullptr);
        if (!heading_ || !open_button_ || !repair_button_) {
            throw std::runtime_error("Cannot create Everything settings controls");
        }
        refresh_language(context.localization);
        layout({}, context.dpi, context.font);
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        MoveWindow(window_, bounds.left, bounds.top, bounds.right - bounds.left,
                   bounds.bottom - bounds.top, TRUE);
        const auto width = std::max<LONG>(1, bounds.right - bounds.left);
        MoveWindow(heading_, 0, 0, width, scale(40), TRUE);
        MoveWindow(open_button_, 0, scale(76), std::min<LONG>(width, scale(380)), scale(40), TRUE);
        MoveWindow(repair_button_, 0, scale(132), std::min<LONG>(width, scale(380)), scale(40), TRUE);
        for (auto control : {heading_, open_button_, repair_button_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }
    void show(bool visible) override {
        EnableWindow(open_button_, manager_.components_available());
        EnableWindow(repair_button_, search_ && manager_.components_available());
        ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
    }
    void refresh_language(const Localization& localization) override {
        SetWindowTextW(heading_, localization.text("settings.open_everything_search").data());
        SetWindowTextW(open_button_, localization.text("ui.open_everything").data());
        SetWindowTextW(repair_button_, localization.text("ui.repair_everything").data());
    }

private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* page = reinterpret_cast<EverythingSettingsPage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (page && message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) {
            try {
                if (LOWORD(wparam) == 1) page->open_(window);
                if (LOWORD(wparam) == 2) page->repair_(window);
            } catch (...) {}
            return 0;
        }
        if (page && message == WM_NCDESTROY) page->window_ = nullptr;
        return DefWindowProcW(window, message, wparam, lparam);
    }
    EverythingManager& manager_;
    const EverythingSearch* search_;
    std::function<void(HWND)> open_;
    std::function<void(HWND)> repair_;
    HWND window_ = nullptr;
    HWND heading_ = nullptr;
    HWND open_button_ = nullptr;
    HWND repair_button_ = nullptr;
};

BuiltInHotKey read_hotkey(const SettingsDocument& document) {
    BuiltInHotKey result{{HotKeyGesture{MOD_WIN, static_cast<UINT>(L'S')}, true}, false};
    if (const auto code = document.get(L"EverythingSearchCode")) {
        result.binding = {};
        const auto comma = code->find(L',');
        try {
            if (comma != std::wstring::npos) {
                const auto modifiers = static_cast<UINT>(std::stoul(code->substr(0, comma)));
                const auto key = static_cast<UINT>(std::stoul(code->substr(comma + 1)));
                if (key != 0 && key <= 0xFF) {
                    result.binding.gesture = {modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN), key};
                }
            }
        } catch (...) {}
    }
    const auto boolean = [&](std::wstring_view key) {
        auto value = document.get(key).value_or(L"0");
        std::ranges::transform(value, value.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(towlower(ch));
        });
        return value == L"1" || value == L"true" || value == L"yes" || value == L"on";
    };
    result.enabled = boolean(L"EverythingSearchEnabled");
    result.binding.force_override = result.binding.gesture
        && (boolean(L"EverythingSearchForce")
            || is_supported_windows_letter_hotkey(*result.binding.gesture));
    if (result.binding.gesture
        && windows_letter_hotkey_index(*result.binding.gesture) == static_cast<std::size_t>(L'L' - L'A')) {
        result.binding = {};
    }
    return result;
}

void write_hotkey(SettingsDocument& document, const BuiltInHotKey& hotkey) {
    std::wstring code;
    if (hotkey.binding.gesture) {
        code = std::to_wstring(hotkey.binding.gesture->modifiers) + L","
            + std::to_wstring(hotkey.binding.gesture->virtual_key);
    }
    document.set(L"Hotkeys", L"EverythingSearchCode", code);
    document.set(L"Hotkeys", L"EverythingSearchForce", hotkey.binding.force_override ? L"1" : L"0");
    document.set(L"Hotkeys", L"EverythingSearchEnabled", hotkey.enabled ? L"1" : L"0");
}

class EverythingModule final : public IAppModule {
public:
    EverythingModule(std::filesystem::path directory, const Localization& localization,
                     ProgramSearchRegistry& search,
                     TrayMenuRegistry& tray, UiDispatcher& dispatcher,
                     HotkeyRegistry& hotkeys, SettingsRegistry& pages,
                     SettingsParticipantRegistry& participants,
                     const SettingsDocument& document,
                     std::function<void(std::wstring_view)> diagnose)
        : directory_(std::move(directory)), localization_(localization),
          search_registry_(search), tray_(tray),
          dispatcher_(dispatcher), diagnose_(std::move(diagnose)),
          manager_(directory_, diagnose_), hotkeys_(hotkeys), pages_(pages), participants_(participants),
          hotkey_(read_hotkey(document)), draft_(hotkey_) {}

    ~EverythingModule() override { stop(); }

    void start() override {
        if (scope_) return;
        scope_ = std::make_unique<DispatchScope>();
        settings_registration_ = participants_.add("everything", 60, SettingsParticipant{
            .begin = [this] { draft_ = hotkey_; },
            .dirty = [this] { return draft_ != hotkey_; },
            .prepare = [this] { previous_ = hotkey_; return true; },
            .apply = [this] { hotkey_ = draft_; return true; },
            .write = [this](auto& document) { write_hotkey(document, draft_); },
            .rollback = [this] { hotkey_ = previous_; return true; },
            .cancel = [this] { draft_ = hotkey_; }});
        hotkey_registration_ = hotkeys_.add("everything.search", 40, HotkeyContribution{
            "settings.open_everything_search",
            [this] { return hotkey_; },
            [this](HWND owner) { show(owner); },
            [this] { return &draft_; }});
        search_ = EverythingSearch::try_create(directory_ / L"Everything64.dll");
        page_registration_ = pages_.add("everything", 60, SettingsPageContribution{
            "settings.open_everything_search", [this] {
                return std::make_unique<EverythingSettingsPage>(
                    manager_, search_.get(), [this](HWND owner) { show(owner); },
                    [this](HWND owner) { repair(owner); });
            }});
        search_command_ = tray_.add("everything.search", 20, TrayCommand{
            "ui.open_everything", "ui.maintenance",
            [this] { return manager_.components_available(); },
            [this](HWND owner) { show(owner); }});
        repair_command_ = tray_.add("everything.repair", 30, TrayCommand{
            "ui.repair_everything", "ui.maintenance",
            [this] { return search_ && manager_.components_available(); },
            [this](HWND owner) { repair(owner); }});
        if (!search_) {
            log(L"everything SDK could not be loaded; file search disabled");
            return;
        }
        provider_ = search_registry_.add("everything", *search_);
        if (manager_.request_start(*search_)) {
            search_registry_.notify_changed();
        } else {
            deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            ready_timer_ = dispatcher_.repeat(*scope_, 250, [this] {
                if (search_->available()) {
                    ready_timer_.reset();
                    log(L"everything database became ready");
                    search_registry_.notify_changed();
                } else if (std::chrono::steady_clock::now() >= deadline_) {
                    ready_timer_.reset();
                    log(L"everything database readiness timed out; continuing without file search");
                }
            });
        }
    }

    void stop() noexcept override {
        if (scope_) scope_->cancel();
        ready_timer_.reset();
        hotkey_registration_.reset();
        page_registration_.reset();
        settings_registration_.reset();
        search_command_.reset();
        repair_command_.reset();
        provider_.reset();
        search_.reset();
        scope_.reset();
    }

private:
    void log(std::wstring_view message) const noexcept {
        try { if (diagnose_) diagnose_(message); } catch (...) {}
    }
    void show(HWND owner) {
        log(L"opening Everything search window through the built-in action");
        if (manager_.show_window(owner)) return;
        MessageBoxW(owner, localization_.text("ui.everything_unavailable").data(),
                    localization_.text("ui.app_title").data(), MB_OK | MB_ICONWARNING);
    }
    void repair(HWND owner) {
        const bool repaired = search_ && manager_.repair_service(*search_, owner);
        MessageBoxW(owner, localization_.text(repaired ? "ui.repair_everything_success"
                                                     : "ui.repair_everything_failed").data(),
                    localization_.text("ui.app_title").data(),
                    MB_OK | (repaired ? MB_ICONINFORMATION : MB_ICONWARNING));
        if (repaired) search_registry_.notify_changed();
    }

    std::filesystem::path directory_;
    const Localization& localization_;
    ProgramSearchRegistry& search_registry_;
    TrayMenuRegistry& tray_;
    UiDispatcher& dispatcher_;
    std::function<void(std::wstring_view)> diagnose_;
    EverythingManager manager_;
    HotkeyRegistry& hotkeys_;
    SettingsRegistry& pages_;
    SettingsParticipantRegistry& participants_;
    BuiltInHotKey hotkey_;
    BuiltInHotKey draft_;
    BuiltInHotKey previous_;
    Registration settings_registration_;
    Registration page_registration_;
    Registration hotkey_registration_;
    std::unique_ptr<EverythingSearch> search_;
    std::unique_ptr<DispatchScope> scope_;
    Registration provider_;
    Registration search_command_;
    Registration repair_command_;
    Registration ready_timer_;
    std::chrono::steady_clock::time_point deadline_{};
};

} // namespace

std::unique_ptr<IAppModule> make_everything_module(
    std::filesystem::path directory, const Localization& localization,
    ProgramSearchRegistry& search, TrayMenuRegistry& tray,
    UiDispatcher& dispatcher, HotkeyRegistry& hotkeys,
    SettingsRegistry& pages, SettingsParticipantRegistry& participants,
    const SettingsDocument& document,
    std::function<void(std::wstring_view)> diagnose) {
    return std::make_unique<EverythingModule>(
        std::move(directory), localization, search, tray, dispatcher,
        hotkeys, pages, participants, document, std::move(diagnose));
}

} // namespace simpilot
