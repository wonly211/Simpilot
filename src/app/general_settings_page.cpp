#include "general_settings_page.hpp"
#include "settings_visual_style.hpp"
#include "toggle_switch.hpp"
#include <commctrl.h>
namespace simpilot {
namespace {
class GeneralPage final : public ISettingsPage {
public:
    explicit GeneralPage(AppSettings& draft) : draft_(draft) {}
    ~GeneralPage() override { if (window_) DestroyWindow(window_); }
    void create(const SettingsPageContext& context) override {
        changed_ = context.changed; change_language_ = context.change_language;
        WNDCLASSW wc{};
        wc.hInstance = context.instance; wc.lpfnWndProc = procedure;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"Simpilot.GeneralSettingsPage";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Cannot register general settings page");
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, context.parent, nullptr, context.instance, this);
        if (!window_) throw std::runtime_error("Cannot create general settings page");
        const auto control = [&](const wchar_t* type, int id, DWORD style) {
            return CreateWindowW(type, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), context.instance, nullptr);
        };
        heading_ = control(L"STATIC", 0, 0);
        startup_label_ = control(L"STATIC", 11, SS_LEFT);
        startup_ = toggle_switch::create(context.instance, window_, 10, L"",
                                         draft_.start_with_windows);
        language_label_ = control(L"STATIC", 0, SS_CENTERIMAGE);
        language_ = control(WC_COMBOBOXW, 12, WS_TABSTOP | CBS_DROPDOWNLIST);
        language_hint_ = control(L"STATIC", 0, SS_LEFT);
        SendMessageW(startup_, BM_SETCHECK, draft_.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
        refresh_language(context.localization);
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        bounds_ = bounds;
        dpi_ = dpi;
        font_ = font;
        typography_.update(font, dpi);
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        const int width = std::min(scale(640), static_cast<int>(std::max(1L, bounds.right - bounds.left)));
        MoveWindow(window_, bounds.left, bounds.top, width, bounds.bottom - bounds.top, TRUE);
        for (auto control : {startup_, startup_label_, language_label_, language_})
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.body()), TRUE);
        SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.title()), TRUE);
        MoveWindow(heading_, 0, 0, width, scale(36), TRUE);

        const int toggle_width = scale(44);
        const int label_width = std::max(1, width - toggle_width - scale(24));
        const auto dc = GetDC(window_);
        const auto previous = SelectObject(dc, typography_.body());
        std::wstring startup_text(static_cast<std::size_t>(GetWindowTextLengthW(startup_label_)) + 1, L'\0');
        GetWindowTextW(startup_label_, startup_text.data(), static_cast<int>(startup_text.size()));
        RECT text{0, 0, label_width, 0};
        DrawTextW(dc, startup_text.c_str(), -1, &text, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        const int row_height = std::max<int>(scale(64), text.bottom + scale(24));
        MoveWindow(startup_label_, 0, scale(56) + (row_height - text.bottom) / 2,
                   label_width, text.bottom, TRUE);
        MoveWindow(startup_, std::max(0, width - toggle_width),
                   scale(56) + (row_height - scale(32)) / 2, toggle_width, scale(32), TRUE);
        separator_ = scale(56) + row_height;
        int language_width = scale(192);
        for (const auto& language : languages_) {
            SIZE size{};
            GetTextExtentPoint32W(dc, language.display_name.c_str(),
                                 static_cast<int>(language.display_name.size()), &size);
            language_width = std::max<int>(language_width, size.cx + scale(44));
        }
        SelectObject(dc, previous);
        ReleaseDC(window_, dc);
        language_width = std::min({language_width, scale(240), width / 2});
        MoveWindow(language_label_, 0, separator_ + scale(14),
                   std::max(1, width - language_width - scale(24)), scale(20), TRUE);
        SendMessageW(language_hint_, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.caption()), TRUE);
        MoveWindow(language_hint_, 0, separator_ + scale(38),
                   std::max(1, width - language_width - scale(24)), scale(18), TRUE);
        SendMessageW(language_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), scale(26));
        SendMessageW(language_, CB_SETITEMHEIGHT, 0, scale(24));
        SendMessageW(language_, CB_SETDROPPEDWIDTH, language_width, 0);
        MoveWindow(language_, width - language_width, separator_ + scale(16),
                   language_width, scale(220), TRUE);
        InvalidateRect(window_, nullptr, TRUE);
    }
    void show(bool visible) override { ShowWindow(window_, visible ? SW_SHOW : SW_HIDE); }
    void refresh_language(const Localization& localization) override {
        current_language_ = std::string(localization.language_code());
        SetWindowTextW(heading_, localization.text("settings.heading").data());
        SetWindowTextW(startup_, localization.text("settings.startup").data());
        SetWindowTextW(startup_label_, localization.text("settings.startup").data());
        SetWindowTextW(language_label_, localization.text("settings.display_language").data());
        SetWindowTextW(language_hint_, localization.text("settings.language_immediate").data());
        languages_ = localization.available_languages();
        SendMessageW(language_, CB_RESETCONTENT, 0, 0);
        for (std::size_t i = 0; i < languages_.size(); ++i) {
            SendMessageW(language_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(languages_[i].display_name.c_str()));
            if (languages_[i].code == localization.language_code()) SendMessageW(language_, CB_SETCURSEL, i, 0);
        }
        if (font_) layout(bounds_, dpi_, font_);
    }
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
        auto* page = reinterpret_cast<GeneralPage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!page) return DefWindowProcW(window, message, wparam, lparam);
        try {
            if (message == WM_COMMAND && LOWORD(wparam) == 10 && HIWORD(wparam) == BN_CLICKED) {
                page->draft_.start_with_windows = SendMessageW(page->startup_, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (page->changed_) page->changed_();
                return 0;
            }
            if (message == WM_COMMAND && LOWORD(wparam) == 12 && HIWORD(wparam) == CBN_SELCHANGE) {
                const auto index = SendMessageW(page->language_, CB_GETCURSEL, 0, 0);
                if (index >= 0 && static_cast<std::size_t>(index) < page->languages_.size() && page->change_language_) {
                    const auto code = page->languages_[index].code;
                    if (!page->change_language_(code)) for (std::size_t i = 0; i < page->languages_.size(); ++i)
                        if (page->languages_[i].code == page->current_language_) SendMessageW(page->language_, CB_SETCURSEL, i, 0);
                }
                return 0;
            }
            if (message == WM_NCDESTROY) page->window_ = nullptr;
            if (message == WM_PAINT) {
                PAINTSTRUCT painting{};
                const auto dc = BeginPaint(window, &painting);
                RECT client{};
                GetClientRect(window, &client);
                settings_visual_style::draw_separator(dc, 0, client.right, page->separator_, page->dpi_);
                EndPaint(window, &painting);
                return 0;
            }
            if (message == WM_ERASEBKGND) return settings_visual_style::erase_background(window, wparam);
            if (message == WM_CTLCOLORSTATIC && reinterpret_cast<HWND>(lparam) == page->language_hint_)
                return settings_visual_style::handle_secondary_text(wparam, lparam);
            if (settings_visual_style::is_color_message(message))
                return settings_visual_style::handle_color_message(message, wparam, lparam);
        } catch (...) {}
        return DefWindowProcW(window, message, wparam, lparam);
    }
    AppSettings& draft_;
    HWND window_ = nullptr, heading_ = nullptr, startup_ = nullptr, startup_label_ = nullptr,
         language_label_ = nullptr, language_ = nullptr, language_hint_ = nullptr;
    settings_visual_style::PageTypography typography_;
    RECT bounds_{};
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    int separator_ = 0;
    std::vector<LanguageInfo> languages_;
    std::string current_language_;
    std::function<void()> changed_;
    std::function<bool(std::string)> change_language_;
};
}
std::unique_ptr<ISettingsPage> make_general_settings_page(AppSettings& draft) {
    return std::make_unique<GeneralPage>(draft);
}
} // namespace simpilot
