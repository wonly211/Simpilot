#include "mouse_locator_module.hpp"

#include "cursor_locator.hpp"
#include "settings_visual_style.hpp"
#include "toggle_switch.hpp"

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
        label_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(2), context.instance, nullptr);
        toggle_ = toggle_switch::create(context.instance, window_, 1, L"", draft_.enabled);
        description_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(3), context.instance, nullptr);
        if (!heading_ || !label_ || !toggle_ || !description_) {
            throw std::runtime_error("Cannot create mouse locator settings controls");
        }
        SendMessageW(toggle_, BM_SETCHECK, draft_.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        refresh_language(context.localization);
        layout({}, context.dpi, context.font);
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        bounds_ = bounds;
        dpi_ = dpi;
        font_ = font;
        typography_.update(font, dpi);
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        const auto width = std::min<LONG>(scale(640), std::max<LONG>(1, bounds.right - bounds.left));
        MoveWindow(window_, bounds.left, bounds.top, width,
                   bounds.bottom - bounds.top, TRUE);
        MoveWindow(heading_, 0, 0, width, scale(36), TRUE);
        SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.title()), TRUE);
        for (auto control : {label_, toggle_, description_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.body()), TRUE);
        }
        SendMessageW(description_, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.caption()), TRUE);
        const int label_width = std::max(1L, width - scale(72));
        const auto dc = GetDC(window_);
        const auto previous = SelectObject(dc, typography_.body());
        const auto measured_height = [&](HWND control) {
            std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
            GetWindowTextW(control, text.data(), static_cast<int>(text.size()));
            RECT area{0, 0, label_width, 0};
            DrawTextW(dc, text.c_str(), -1, &area, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            return area.bottom;
        };
        const int label_height = measured_height(label_);
        SelectObject(dc, typography_.caption());
        const int description_height = measured_height(description_);
        SelectObject(dc, previous);
        ReleaseDC(window_, dc);
        MoveWindow(label_, 0, scale(68), label_width, label_height, TRUE);
        MoveWindow(description_, 0, scale(72) + label_height,
                   label_width, description_height, TRUE);
        const int row_height = std::max(scale(72), label_height + description_height + scale(28));
        MoveWindow(toggle_, std::max(0L, width - scale(44)),
                   scale(56) + (row_height - scale(32)) / 2, scale(44), scale(32), TRUE);
        separator_ = scale(56) + row_height;
        InvalidateRect(window_, nullptr, TRUE);
    }
    void show(bool visible) override { ShowWindow(window_, visible ? SW_SHOW : SW_HIDE); }
    void refresh_language(const Localization& localization) override {
        SetWindowTextW(heading_, localization.text("settings.section.cursor_locator").data());
        SetWindowTextW(toggle_, localization.text("settings.cursor_locator").data());
        SetWindowTextW(label_, localization.text("settings.cursor_locator").data());
        SetWindowTextW(description_, localization.text("settings.cursor_locator.description").data());
        if (font_) layout(bounds_, dpi_, font_);
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
        if (page && message == WM_PAINT) {
            PAINTSTRUCT painting{};
            const auto dc = BeginPaint(window, &painting);
            RECT client{};
            GetClientRect(window, &client);
            settings_visual_style::draw_separator(dc, 0, client.right, page->separator_, page->dpi_);
            EndPaint(window, &painting);
            return 0;
        }
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
    HWND label_ = nullptr;
    HWND description_ = nullptr;
    settings_visual_style::PageTypography typography_;
    RECT bounds_{};
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    int separator_ = 0;
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
