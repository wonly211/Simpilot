#include "general_settings_page.hpp"
#include "settings_visual_style.hpp"
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
        startup_ = control(L"BUTTON", 10, WS_TABSTOP | BS_AUTOCHECKBOX);
        language_label_ = control(L"STATIC", 0, SS_CENTERIMAGE);
        language_ = control(WC_COMBOBOXW, 12, WS_TABSTOP | CBS_DROPDOWNLIST);
        SendMessageW(startup_, BM_SETCHECK, draft_.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
        refresh_language(context.localization);
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        const int width = std::max(1L, bounds.right - bounds.left);
        MoveWindow(window_, bounds.left, bounds.top, width, bounds.bottom - bounds.top, TRUE);
        MoveWindow(heading_, 0, 0, width, scale(38), TRUE);
        MoveWindow(startup_, 0, scale(72), width, scale(38), TRUE);
        MoveWindow(language_label_, 0, scale(142), width / 3, scale(34), TRUE);
        MoveWindow(language_, width / 3, scale(142), std::min(scale(280), width * 2 / 3), scale(180), TRUE);
        for (auto control : {heading_, startup_, language_label_, language_})
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    void show(bool visible) override { ShowWindow(window_, visible ? SW_SHOW : SW_HIDE); }
    void refresh_language(const Localization& localization) override {
        current_language_ = std::string(localization.language_code());
        SetWindowTextW(heading_, localization.text("settings.heading").data());
        SetWindowTextW(startup_, localization.text("settings.startup").data());
        SetWindowTextW(language_label_, localization.text("settings.display_language").data());
        languages_ = localization.available_languages();
        SendMessageW(language_, CB_RESETCONTENT, 0, 0);
        for (std::size_t i = 0; i < languages_.size(); ++i) {
            SendMessageW(language_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(languages_[i].display_name.c_str()));
            if (languages_[i].code == localization.language_code()) SendMessageW(language_, CB_SETCURSEL, i, 0);
        }
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
            if (message == WM_ERASEBKGND) return settings_visual_style::erase_background(window, wparam);
            if (settings_visual_style::is_color_message(message))
                return settings_visual_style::handle_color_message(message, wparam, lparam);
        } catch (...) {}
        return DefWindowProcW(window, message, wparam, lparam);
    }
    AppSettings& draft_;
    HWND window_ = nullptr, heading_ = nullptr, startup_ = nullptr, language_label_ = nullptr, language_ = nullptr;
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
