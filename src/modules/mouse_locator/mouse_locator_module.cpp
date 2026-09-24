#include "mouse_locator_module.hpp"

#include "cursor_locator.hpp"
#include "settings_visual_style.hpp"

#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace simpilot {
namespace {

class MouseLocatorSettingsPage final : public ISettingsPage {
public:
    explicit MouseLocatorSettingsPage(MouseLocatorSettings& draft) : draft_(draft) {}
    ~MouseLocatorSettingsPage() override { if (window_) DestroyWindow(window_); }

    void create(const SettingsPageContext& context) override {
        changed_ = context.changed;
        WNDCLASSW window_class{
            .lpfnWndProc = &MouseLocatorSettingsPage::procedure,
            .hInstance = context.instance,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
            .lpszClassName = L"Simpilot.MouseLocatorSettingsPage",
        };
        if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Cannot register mouse locator settings page");
        }
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, window_class.lpszClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, context.parent, nullptr,
            context.instance, this);
        if (!window_) throw std::runtime_error("Cannot create mouse locator settings page");
        heading_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window_, nullptr, context.instance, nullptr);
        toggle_ = CreateWindowW(L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(1), context.instance, nullptr);
        description_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window_, nullptr, context.instance, nullptr);
        if (!heading_ || !toggle_ || !description_) {
            throw std::runtime_error("Cannot create mouse locator settings controls");
        }
        SendMessageW(toggle_, BM_SETCHECK, draft_.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        refresh_language(context.localization);
        layout({}, context.dpi, context.font);
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        MoveWindow(window_, bounds.left, bounds.top, bounds.right - bounds.left,
                   bounds.bottom - bounds.top, TRUE);
        const auto width = std::max<LONG>(1, bounds.right - bounds.left);
        MoveWindow(heading_, 0, 0, width, scale(40), TRUE);
        MoveWindow(toggle_, 0, scale(76), width, scale(48), TRUE);
        MoveWindow(description_, 0, scale(140), width, scale(96), TRUE);
        for (auto control : {heading_, toggle_, description_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }
    void show(bool visible) override { ShowWindow(window_, visible ? SW_SHOW : SW_HIDE); }
    void refresh_language(const Localization& localization) override {
        SetWindowTextW(heading_, localization.text("settings.section.cursor_locator").data());
        SetWindowTextW(toggle_, localization.text("settings.cursor_locator").data());
        SetWindowTextW(description_, localization.text("settings.cursor_locator.description").data());
    }

private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* page = reinterpret_cast<MouseLocatorSettingsPage*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_ERASEBKGND) return settings_visual_style::erase_background(window, wparam);
        if (page && message == WM_CTLCOLORSTATIC
            && reinterpret_cast<HWND>(lparam) == page->description_) {
            return settings_visual_style::handle_secondary_text(wparam, lparam);
        }
        if (settings_visual_style::is_color_message(message)) {
            return settings_visual_style::handle_color_message(message, wparam, lparam);
        }
        if (page && message == WM_COMMAND && LOWORD(wparam) == 1
            && HIWORD(wparam) == BN_CLICKED) {
            page->draft_.enabled = SendMessageW(page->toggle_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            try { if (page->changed_) page->changed_(); } catch (...) {}
            return 0;
        }
        if (page && message == WM_NCDESTROY) page->window_ = nullptr;
        return DefWindowProcW(window, message, wparam, lparam);
    }
    MouseLocatorSettings& draft_;
    std::function<void()> changed_;
    HWND window_ = nullptr;
    HWND heading_ = nullptr;
    HWND toggle_ = nullptr;
    HWND description_ = nullptr;
};

class MouseLocatorModule final : public IAppModule {
public:
    MouseLocatorModule(HINSTANCE instance, MouseLocatorSettings settings,
                       SettingsRegistry& pages, SettingsParticipantRegistry& participants,
                       std::function<void(std::wstring_view)> diagnose)
        : settings_(settings), draft_(settings), locator_(instance), pages_(pages),
          participants_(participants), diagnose_(std::move(diagnose)) {}
    ~MouseLocatorModule() override { stop(); }
    void start() override {
        if (started_) return;
        started_ = true;
        if (!locator_.set_enabled(settings_.enabled)) {
            log(L"mouse shake cursor locator could not start");
        }
        participant_ = participants_.add("mouse_locator", 70, SettingsParticipant{
            .begin = [this] { draft_ = settings_; },
            .dirty = [this] { return draft_ != settings_; },
            .prepare = [this] { previous_enabled_ = locator_.enabled(); return true; },
            .apply = [this] {
                const auto applied = locator_.set_enabled(draft_.enabled);
                if (!applied) log(L"mouse shake cursor locator update failed");
                return applied;
            },
            .write = [this](SettingsDocument& document) { draft_.write(document); },
            .rollback = [this] { return locator_.set_enabled(previous_enabled_); },
            .finish = [this] { settings_ = draft_; },
            .cancel = [this] { draft_ = settings_; }});
        page_ = pages_.add("mouse_locator", 70, SettingsPageContribution{
            "settings.section.cursor_locator",
            [this] { return std::make_unique<MouseLocatorSettingsPage>(draft_); }});
    }
    void stop() noexcept override {
        page_.reset();
        participant_.reset();
        (void)locator_.set_enabled(false);
        started_ = false;
    }

private:
    void log(std::wstring_view message) const noexcept {
        try { if (diagnose_) diagnose_(message); } catch (...) {}
    }
    MouseLocatorSettings settings_;
    MouseLocatorSettings draft_;
    CursorLocator locator_;
    SettingsRegistry& pages_;
    SettingsParticipantRegistry& participants_;
    std::function<void(std::wstring_view)> diagnose_;
    Registration participant_;
    Registration page_;
    bool previous_enabled_ = false;
    bool started_ = false;
};

} // namespace

MouseLocatorSettings MouseLocatorSettings::read(const SettingsDocument& document) {
    auto value = document.get(L"MouseShakeLocatorEnabled").value_or(L"0");
    std::ranges::transform(value, value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(towlower(character));
    });
    return {value == L"1" || value == L"true" || value == L"yes" || value == L"on"};
}

void MouseLocatorSettings::write(SettingsDocument& document) const {
    document.set(L"General", L"MouseShakeLocatorEnabled", enabled ? L"1" : L"0");
}

std::unique_ptr<IAppModule> make_mouse_locator_module(
    HINSTANCE instance, const SettingsDocument& document, SettingsRegistry& pages,
    SettingsParticipantRegistry& participants,
    std::function<void(std::wstring_view)> diagnose) {
    return std::make_unique<MouseLocatorModule>(
        instance, MouseLocatorSettings::read(document), pages, participants, std::move(diagnose));
}

} // namespace simpilot
