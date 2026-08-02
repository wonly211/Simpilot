#pragma once

#include <Windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <string>

namespace simpilot::settings_visual_style {

inline bool high_contrast_enabled() noexcept {
    HIGHCONTRASTW state{.cbSize = sizeof(state)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(state), &state, 0)
        && (state.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

inline COLORREF background_color() noexcept {
    return high_contrast_enabled() ? GetSysColor(COLOR_WINDOW) : RGB(245, 246, 247);
}

inline COLORREF surface_color() noexcept {
    return high_contrast_enabled() ? GetSysColor(COLOR_WINDOW) : RGB(255, 255, 255);
}

inline COLORREF text_color(const HWND control) noexcept {
    if (control && !IsWindowEnabled(control)) return GetSysColor(COLOR_GRAYTEXT);
    return high_contrast_enabled() ? GetSysColor(COLOR_WINDOWTEXT) : RGB(32, 32, 32);
}

struct Brushes final {
    Brushes()
        : background(CreateSolidBrush(RGB(245, 246, 247))),
          surface(CreateSolidBrush(RGB(255, 255, 255))),
          navigation(CreateSolidBrush(RGB(239, 241, 243))),
          navigation_selected(CreateSolidBrush(RGB(229, 240, 250))),
          accent(CreateSolidBrush(RGB(0, 103, 192))) {}

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

inline LRESULT handle_navigation_color(const WPARAM wparam) noexcept {
    const auto dc = reinterpret_cast<HDC>(wparam);
    const auto color = high_contrast_enabled() ? GetSysColor(COLOR_WINDOW) : RGB(239, 241, 243);
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
    FillRect(drawing.hDC, &drawing.rcItem, background);
    if (selected && !high_contrast) {
        RECT accent_rectangle = drawing.rcItem;
        accent_rectangle.right = accent_rectangle.left + 3;
        FillRect(drawing.hDC, &accent_rectangle, brushes().accent);
    }
    const auto length = static_cast<int>(SendMessageW(
        list, LB_GETTEXTLEN, drawing.itemID, 0));
    std::wstring label(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
    SendMessageW(list, LB_GETTEXT, drawing.itemID,
                 reinterpret_cast<LPARAM>(label.data()));
    label.resize(static_cast<std::size_t>(std::max(0, length)));
    RECT text_rectangle = drawing.rcItem;
    text_rectangle.left += 18;
    text_rectangle.right -= 10;
    SetBkMode(drawing.hDC, TRANSPARENT);
    SetTextColor(drawing.hDC, high_contrast && selected
        ? GetSysColor(COLOR_HIGHLIGHTTEXT) : text_color(list));
    DrawTextW(drawing.hDC, label.c_str(), static_cast<int>(label.size()),
              &text_rectangle, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
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
        : high_contrast_enabled() ? GetSysColor(COLOR_WINDOWTEXT) : RGB(96, 96, 96);
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

inline void style_list_view(const HWND list) noexcept {
    ListView_SetBkColor(list, surface_color());
    ListView_SetTextBkColor(list, surface_color());
    ListView_SetTextColor(list, text_color(list));
}

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
