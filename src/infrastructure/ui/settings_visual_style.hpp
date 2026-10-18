#pragma once

#include <Windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cwchar>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

namespace simpilot::settings_visual_style {

class PageTypography final {
public:
    ~PageTypography() { release(); }
    PageTypography() = default;
    PageTypography(const PageTypography&) = delete;
    PageTypography& operator=(const PageTypography&) = delete;
    void update(HFONT body, UINT dpi) {
        LOGFONTW base{};
        if (!body || !GetObjectW(body, sizeof(base), &base)) {
            base.lfCharSet = DEFAULT_CHARSET;
            wcscpy_s(base.lfFaceName, L"Segoe UI");
        }
        if (dpi_ == dpi && std::memcmp(&base_, &base, sizeof(base)) == 0) return;
        release();
        base_ = base;
        dpi_ = dpi;
        const auto make = [&](int size, int weight) {
            auto value = base;
            value.lfHeight = -MulDiv(size, dpi, 96);
            value.lfWidth = 0;
            value.lfWeight = weight;
            value.lfQuality = CLEARTYPE_QUALITY;
            return CreateFontIndirectW(&value);
        };
        body_ = make(14, FW_NORMAL);
        title_ = make(24, FW_SEMIBOLD);
        section_ = make(14, FW_SEMIBOLD);
        caption_ = make(12, FW_NORMAL);
    }
    HFONT body() const { return body_; }
    HFONT title() const { return title_; }
    HFONT section() const { return section_; }
    HFONT caption() const { return caption_; }
private:
    void release() {
        for (auto font : {body_, title_, section_, caption_}) if (font) DeleteObject(font);
        body_ = title_ = section_ = caption_ = nullptr;
    }
    LOGFONTW base_{};
    UINT dpi_ = 0;
    HFONT body_ = nullptr, title_ = nullptr, section_ = nullptr, caption_ = nullptr;
};

enum class ButtonStyle {
    secondary, primary, quiet, clear, forward, capture, recording,
    add, edit, remove, up, down, indent, outdent
};
// Native buttons retain their keyboard, focus and accessibility behavior.
void style_button(HWND button, ButtonStyle style = ButtonStyle::secondary);
void set_button_tooltip(HWND button, std::wstring_view text);

inline bool high_contrast_enabled() noexcept {
    HIGHCONTRASTW state{.cbSize = sizeof(state)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(state), &state, 0)
        && (state.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

inline COLORREF background_color() noexcept {
    return high_contrast_enabled() ? GetSysColor(COLOR_WINDOW) : RGB(247, 248, 250);
}

inline COLORREF surface_color() noexcept {
    return high_contrast_enabled() ? GetSysColor(COLOR_WINDOW) : RGB(255, 255, 255);
}

inline COLORREF text_color(const HWND control) noexcept {
    if (control && !IsWindowEnabled(control))
        return high_contrast_enabled() ? GetSysColor(COLOR_GRAYTEXT) : RGB(133, 139, 148);
    return high_contrast_enabled() ? GetSysColor(COLOR_WINDOWTEXT) : RGB(32, 33, 36);
}

struct Brushes final {
    Brushes()
        : background(CreateSolidBrush(RGB(247, 248, 250))),
          surface(CreateSolidBrush(RGB(255, 255, 255))),
          navigation(CreateSolidBrush(RGB(240, 241, 243))),
          navigation_selected(CreateSolidBrush(RGB(231, 241, 250))),
          accent(CreateSolidBrush(RGB(0, 103, 184))) {}

    ~Brushes() {
        if (background) DeleteObject(background);
        if (surface) DeleteObject(surface);
        if (navigation) DeleteObject(navigation);
        if (navigation_selected) DeleteObject(navigation_selected);
        if (accent) DeleteObject(accent);
    }

    HBRUSH background = nullptr;
    HBRUSH surface = nullptr;
    HBRUSH navigation = nullptr;
    HBRUSH navigation_selected = nullptr;
    HBRUSH accent = nullptr;
};

inline const Brushes& brushes() noexcept {
    static const Brushes value;
    return value;
}

inline HBRUSH background_brush() noexcept {
    return high_contrast_enabled() ? GetSysColorBrush(COLOR_WINDOW) : brushes().background;
}

inline HBRUSH surface_brush() noexcept {
    return high_contrast_enabled() ? GetSysColorBrush(COLOR_WINDOW) : brushes().surface;
}

inline void draw_separator(HDC dc, int left, int right, int y, UINT dpi = 96) noexcept {
    const RECT line{left, y, right, y + MulDiv(1, dpi, 96)};
    const auto brush = CreateSolidBrush(high_contrast_enabled()
        ? GetSysColor(COLOR_WINDOWTEXT) : RGB(227, 230, 234));
    FillRect(dc, &line, brush);
    DeleteObject(brush);
}

inline LRESULT handle_navigation_color(const WPARAM wparam) noexcept {
    const auto dc = reinterpret_cast<HDC>(wparam);
    const auto color = high_contrast_enabled() ? GetSysColor(COLOR_WINDOW) : RGB(240, 241, 243);
    SetTextColor(dc, text_color(nullptr));
    SetBkColor(dc, color);
    return reinterpret_cast<LRESULT>(
        high_contrast_enabled() ? GetSysColorBrush(COLOR_WINDOW) : brushes().navigation);
}

inline void draw_navigation_item(const DRAWITEMSTRUCT& drawing, const HWND list) {
    if (drawing.itemID == static_cast<UINT>(-1)) return;
    const auto selected = (drawing.itemState & ODS_SELECTED) != 0;
    const auto high_contrast = high_contrast_enabled();
    const auto background = high_contrast
        ? GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW)
        : selected ? brushes().navigation_selected : brushes().navigation;
    const auto dpi = GetDpiForWindow(list);
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    FillRect(drawing.hDC, &drawing.rcItem, high_contrast
        ? GetSysColorBrush(COLOR_WINDOW) : brushes().navigation);
    RECT item = drawing.rcItem;
    InflateRect(&item, 0, -scale(2));
    const auto old_brush = SelectObject(drawing.hDC, background);
    const auto old_pen = SelectObject(drawing.hDC, GetStockObject(NULL_PEN));
    RoundRect(drawing.hDC, item.left, item.top, item.right, item.bottom, scale(6), scale(6));
    SelectObject(drawing.hDC, old_pen);
    SelectObject(drawing.hDC, old_brush);
    if (selected && !high_contrast) {
        RECT accent_rectangle = item;
        accent_rectangle.top += (item.bottom - item.top - scale(16)) / 2;
        accent_rectangle.bottom = accent_rectangle.top + scale(16);
        accent_rectangle.right = accent_rectangle.left + scale(3);
        FillRect(drawing.hDC, &accent_rectangle, brushes().accent);
    }
    const auto length = static_cast<int>(SendMessageW(
        list, LB_GETTEXTLEN, drawing.itemID, 0));
    std::wstring label(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
    SendMessageW(list, LB_GETTEXT, drawing.itemID,
                 reinterpret_cast<LPARAM>(label.data()));
    label.resize(static_cast<std::size_t>(std::max(0, length)));
    RECT text_rectangle = drawing.rcItem;
    text_rectangle.left += scale(16);
    text_rectangle.right -= scale(12);
    SetBkMode(drawing.hDC, TRANSPARENT);
    SetTextColor(drawing.hDC, high_contrast && selected
        ? GetSysColor(COLOR_HIGHLIGHTTEXT) : text_color(list));
    const auto font = reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0));
    const auto previous_font = font ? SelectObject(drawing.hDC, font) : nullptr;
    RECT measured = text_rectangle;
    DrawTextW(drawing.hDC, label.c_str(), static_cast<int>(label.size()),
              &measured, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    text_rectangle.top += std::max(0L,
        (text_rectangle.bottom - text_rectangle.top - (measured.bottom - measured.top)) / 2);
    DrawTextW(drawing.hDC, label.c_str(), static_cast<int>(label.size()),
              &text_rectangle, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (previous_font) SelectObject(drawing.hDC, previous_font);
    if ((drawing.itemState & ODS_FOCUS) != 0) {
        RECT focus_rectangle = drawing.rcItem;
        InflateRect(&focus_rectangle, -4, -3);
        DrawFocusRect(drawing.hDC, &focus_rectangle);
    }
}

inline bool is_color_message(const UINT message) noexcept {
    return message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN
        || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX;
}

inline LRESULT handle_color_message(
    const UINT message, const WPARAM wparam, const LPARAM lparam) noexcept {
    const auto dc = reinterpret_cast<HDC>(wparam);
    const auto control = reinterpret_cast<HWND>(lparam);
    wchar_t class_name[16]{};
    if (control) GetClassNameW(control, class_name, static_cast<int>(std::size(class_name)));
    const auto surface = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX
        || (message == WM_CTLCOLORSTATIC && _wcsicmp(class_name, L"Edit") == 0);
    SetTextColor(dc, text_color(control));
    SetBkColor(dc, surface ? surface_color() : background_color());
    SetBkMode(dc, surface ? OPAQUE : TRANSPARENT);
    return reinterpret_cast<LRESULT>(surface ? surface_brush() : background_brush());
}

inline LRESULT handle_secondary_text(const WPARAM wparam, const LPARAM lparam) noexcept {
    const auto dc = reinterpret_cast<HDC>(wparam);
    const auto control = reinterpret_cast<HWND>(lparam);
    const auto color = control && !IsWindowEnabled(control) ? GetSysColor(COLOR_GRAYTEXT)
        : high_contrast_enabled() ? GetSysColor(COLOR_WINDOWTEXT) : RGB(96, 101, 109);
    SetTextColor(dc, color);
    SetBkColor(dc, background_color());
    SetBkMode(dc, TRANSPARENT);
    return reinterpret_cast<LRESULT>(background_brush());
}

inline LRESULT erase_background(const HWND window, const WPARAM wparam) noexcept {
    RECT rectangle{};
    GetClientRect(window, &rectangle);
    FillRect(reinterpret_cast<HDC>(wparam), &rectangle, background_brush());
    return 1;
}

void style_list_view(HWND list) noexcept;
void set_list_empty_text(HWND list, std::wstring_view text);
std::wstring compact_path_text(HDC dc, std::wstring_view text, int width);

inline void style_tree_view(const HWND tree) noexcept {
    TreeView_SetBkColor(tree, surface_color());
    TreeView_SetTextColor(tree, text_color(tree));
}

inline void apply_application_icons(
    const HWND window, const HINSTANCE instance, const WORD resource_identifier) noexcept {
    const auto load_icon = [instance, resource_identifier](const int width, const int height) {
        return static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(resource_identifier), IMAGE_ICON,
            width, height, LR_SHARED));
    };
    if (const auto large_icon = load_icon(
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))) {
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
    }
    if (const auto small_icon = load_icon(
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))) {
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
    }
}

} // namespace simpilot::settings_visual_style
