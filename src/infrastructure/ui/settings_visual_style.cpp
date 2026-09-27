#include "settings_visual_style.hpp"

#include <memory>

namespace simpilot::settings_visual_style {
namespace {
struct ButtonState {
    ButtonStyle style = ButtonStyle::secondary;
    bool hot = false;
    HWND tooltip = nullptr;
    HWND tooltip_owner = nullptr;
    std::wstring tooltip_text;
    ~ButtonState() { if (tooltip) DestroyWindow(tooltip); }
};
constexpr UINT_PTR button_subclass = 0x5350;

const wchar_t* button_glyph(ButtonStyle style) {
    switch (style) {
    case ButtonStyle::add: return L"\xE710";
    case ButtonStyle::edit: return L"\xE70F";
    case ButtonStyle::remove: return L"\xE74D";
    case ButtonStyle::up: return L"\xE74A";
    case ButtonStyle::down: return L"\xE74B";
    case ButtonStyle::indent: return L"\xE72A";
    case ButtonStyle::outdent: return L"\xE72B";
    default: return nullptr;
    }
}

void draw_icon(HDC dc, const wchar_t* glyph, RECT bounds, UINT dpi) {
    const auto font = CreateFontW(-MulDiv(16, dpi, 96), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
    const auto previous = SelectObject(dc, font);
    DrawTextW(dc, glyph, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, previous);
    DeleteObject(font);
}

void paint_button(HWND window, HDC dc, const ButtonState& state) {
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, background_brush());
    const bool enabled = IsWindowEnabled(window) != FALSE;
    const bool pressed = (SendMessageW(window, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
    const bool checked = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool primary = state.style == ButtonStyle::primary;
    const bool quiet = state.style == ButtonStyle::quiet || state.style == ButtonStyle::clear;
    const auto dpi = GetDpiForWindow(window);
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    const auto contrast = high_contrast_enabled();
    const bool capture = state.style == ButtonStyle::capture || state.style == ButtonStyle::recording;
    COLORREF fill = surface_color(), border = RGB(138, 146, 157), text = text_color(window);
    if (primary && enabled) {
        fill = pressed ? RGB(0, 79, 139) : state.hot ? RGB(0, 90, 158) : RGB(0, 103, 184);
        text = RGB(255, 255, 255);
        border = fill;
    } else if (quiet) {
        fill = enabled && pressed ? RGB(226, 230, 235)
            : state.hot && enabled ? RGB(238, 241, 245) : background_color();
        border = fill;
    } else if (!enabled) {
        fill = background_color();
        border = RGB(227, 230, 234);
    } else if (pressed || state.hot) {
        fill = pressed ? RGB(226, 230, 235) : RGB(238, 241, 245);
    }
    if (enabled && checked) {
        fill = pressed ? RGB(226, 230, 235) : RGB(231, 241, 250);
        border = RGB(0, 103, 184);
    }
    if (enabled && state.style == ButtonStyle::recording) {
        fill = RGB(231, 241, 250);
        border = RGB(0, 95, 184);
    }
    if (contrast) {
        fill = GetSysColor((primary || checked) && enabled ? COLOR_HIGHLIGHT : COLOR_WINDOW);
        border = GetSysColor(COLOR_WINDOWTEXT);
        text = GetSysColor(!enabled ? COLOR_GRAYTEXT
            : primary || checked ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
    }
    const auto brush = CreateSolidBrush(fill);
    const auto pen = CreatePen(PS_SOLID, scale(1), border);
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, client.left, client.top, client.right, client.bottom, scale(8), scale(8));
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
    const auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    const auto old_font = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    if (const auto glyph = button_glyph(state.style)) {
        draw_icon(dc, glyph, client, dpi);
    } else if (state.style == ButtonStyle::clear) {
        // Standard close/clear symbol; the native button name and tooltip remain localized.
        const auto glyph_pen = CreatePen(PS_SOLID, std::max(1, scale(1)), text);
        const auto previous = SelectObject(dc, glyph_pen);
        const int cx = client.right / 2, cy = client.bottom / 2, radius = scale(4);
        MoveToEx(dc, cx - radius, cy - radius, nullptr);
        LineTo(dc, cx + radius + 1, cy + radius + 1);
        MoveToEx(dc, cx - radius, cy + radius, nullptr);
        LineTo(dc, cx + radius + 1, cy - radius - 1);
        SelectObject(dc, previous);
        DeleteObject(glyph_pen);
    } else {
        std::wstring text_value(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
        GetWindowTextW(window, text_value.data(), static_cast<int>(text_value.size()));
        if (state.style == ButtonStyle::forward) text_value = L"\x2192";
        InflateRect(&client, -scale(12), 0);
        if (capture) {
            RECT icon_bounds = client;
            icon_bounds.left = icon_bounds.right - scale(16);
            draw_icon(dc, L"\xE765", icon_bounds, dpi);
            client.right = icon_bounds.left - scale(8);
        }
        const auto hide_accel = SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL;
        DrawTextW(dc, text_value.c_str(), -1, &client,
            (capture ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
                | (hide_accel ? DT_HIDEPREFIX : 0));
    }
    if (old_font) SelectObject(dc, old_font);
    if (GetFocus() == window || state.style == ButtonStyle::recording) {
        GetClientRect(window, &client);
        InflateRect(&client, -scale(3), -scale(3));
        if (contrast) DrawFocusRect(dc, &client);
        else {
            const auto focus_pen = CreatePen(PS_SOLID, scale(2), primary
                ? RGB(255, 255, 255) : RGB(0, 95, 184));
            const auto previous_pen = SelectObject(dc, focus_pen);
            const auto previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            RoundRect(dc, client.left, client.top, client.right, client.bottom, scale(4), scale(4));
            SelectObject(dc, previous_brush);
            SelectObject(dc, previous_pen);
            DeleteObject(focus_pen);
        }
    }
}

LRESULT CALLBACK button_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR reference) {
    auto* state = reinterpret_cast<ButtonState*>(reference);
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT painting{};
        const auto dc = BeginPaint(window, &painting);
        paint_button(window, dc, *state);
        EndPaint(window, &painting);
        return 0;
    }
    case WM_PRINTCLIENT:
        paint_button(window, reinterpret_cast<HDC>(wparam), *state);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE:
        if (!state->hot) {
            state->hot = true;
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        state->hot = false;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, button_procedure, button_subclass);
        delete state;
        return DefSubclassProc(window, message, wparam, lparam);
    }
    const auto result = DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_SETTEXT && button_glyph(state->style))
        set_button_tooltip(window, reinterpret_cast<const wchar_t*>(lparam));
    if (message == BM_SETSTATE || message == BM_SETCHECK || message == BM_SETSTYLE || message == WM_ENABLE
        || message == WM_SETTEXT || message == WM_SETFONT || message == WM_SETFOCUS
        || message == WM_KILLFOCUS || message == WM_UPDATEUISTATE
        || message == WM_THEMECHANGED || message == WM_SETTINGCHANGE)
        InvalidateRect(window, nullptr, FALSE);
    return result;
}
}

void style_button(HWND button, ButtonStyle style) {
    DWORD_PTR existing = 0;
    if (GetWindowSubclass(button, button_procedure, button_subclass, &existing)) {
        reinterpret_cast<ButtonState*>(existing)->style = style;
        InvalidateRect(button, nullptr, FALSE);
    } else {
        auto state = std::make_unique<ButtonState>();
        state->style = style;
        if (SetWindowSubclass(button, button_procedure, button_subclass,
            reinterpret_cast<DWORD_PTR>(state.get()))) state.release();
    }
    if (button_glyph(style)) {
        std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(button)) + 1, L'\0');
        GetWindowTextW(button, text.data(), static_cast<int>(text.size()));
        text.resize(wcslen(text.c_str()));
        set_button_tooltip(button, text);
    }
}

void set_button_tooltip(HWND button, std::wstring_view text) {
    DWORD_PTR reference = 0;
    if (!GetWindowSubclass(button, button_procedure, button_subclass, &reference)) return;
    auto& state = *reinterpret_cast<ButtonState*>(reference);
    if (!state.tooltip) {
        state.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT, button, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!state.tooltip) return;
        state.tooltip_owner = GetParent(button);
        SendMessageW(state.tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 500);
    }
    const bool first = state.tooltip_text.empty();
    state.tooltip_text = text;
    TOOLINFOW info{sizeof(info)};
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = state.tooltip_owner;
    info.uId = reinterpret_cast<UINT_PTR>(button);
    info.lpszText = state.tooltip_text.data();
    SendMessageW(state.tooltip, first ? TTM_ADDTOOLW : TTM_UPDATETIPTEXTW, 0,
        reinterpret_cast<LPARAM>(&info));
}

std::wstring compact_path_text(HDC dc, std::wstring_view text, int width) {
    const auto fits = [dc](std::wstring_view value, int available) {
        SIZE extent{};
        return GetTextExtentPoint32W(dc, value.data(), static_cast<int>(value.size()), &extent)
            && extent.cx <= available;
    };
    if (fits(text, width)) return std::wstring(text);
    if (!fits(L"...", width)) return {};
    SIZE ellipsis{};
    GetTextExtentPoint32W(dc, L"...", 3, &ellipsis);
    const int prefix_width = std::min(width / 3, width - static_cast<int>(ellipsis.cx));
    const auto boundary = [text](std::size_t position) {
        return position == 0 || position == text.size()
            || !(text[position - 1] >= 0xD800 && text[position - 1] <= 0xDBFF
                && text[position] >= 0xDC00 && text[position] <= 0xDFFF);
    };
    // Reserve most of the cell for the filename tail, including its extension.
    std::size_t prefix = 0;
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (!boundary(i)) continue;
        if (!fits(text.substr(0, i), prefix_width)) break;
        prefix = i;
    }
    std::size_t low = prefix, high = text.size();
    const auto candidate = [&](std::size_t start) {
        if (!boundary(start)) ++start;
        return std::wstring(text.substr(0, prefix)) + L"..." + std::wstring(text.substr(start));
    };
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        if (fits(candidate(middle), width)) high = middle;
        else low = middle + 1;
    }
    return candidate(low);
}

namespace {
constexpr UINT_PTR list_subclass = 0x5352;
struct ListState {
    HWND owner = nullptr, list = nullptr;
    HWND tooltip = nullptr;
    HIMAGELIST spacing = nullptr;
    HFONT header_font = nullptr;
    std::wstring text, empty_text;
    ~ListState() {
        if (tooltip) DestroyWindow(tooltip);
        if (spacing) ImageList_Destroy(spacing);
        if (header_font) DeleteObject(header_font);
    }
};

std::wstring cell_text(HWND list, int row, int column) {
    std::wstring value(32768, L'\0');
    LVITEMW item{.iSubItem = column, .pszText = value.data(), .cchTextMax = static_cast<int>(value.size())};
    const int count = static_cast<int>(SendMessageW(list, LVM_GETITEMTEXTW, row, reinterpret_cast<LPARAM>(&item)));
    value.resize(std::max(0, count));
    return value;
}

void hide_tip(ListState& state) {
    if (!state.tooltip) return;
    TOOLINFOW tool{sizeof(tool)};
    tool.hwnd = state.list;
    tool.uId = 1;
    SendMessageW(state.tooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
}

void show_row_tip(ListState& state, int row) {
    if (row < 0) return;
    if (!state.tooltip) {
        state.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, state.list,
            nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!state.tooltip) return;
        TOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        tool.hwnd = state.list;
        tool.uId = 1;
        tool.lpszText = const_cast<wchar_t*>(L"");
        SendMessageW(state.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        SendMessageW(state.tooltip, TTM_SETMAXTIPWIDTH, 0, MulDiv(640, GetDpiForWindow(state.list), 96));
    }
    state.text.clear();
    const int columns = Header_GetItemCount(ListView_GetHeader(state.list));
    for (int i = 0; i < columns; ++i) {
        auto cell = cell_text(state.list, row, i);
        if (cell.empty()) continue;
        if (!state.text.empty()) state.text += L"\n";
        state.text += cell;
    }
    TOOLINFOW tool{sizeof(tool)};
    tool.hwnd = state.list;
    tool.uId = 1;
    tool.lpszText = state.text.data();
    SendMessageW(state.tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    RECT row_bounds{};
    ListView_GetItemRect(state.list, row, &row_bounds, LVIR_BOUNDS);
    POINT point{row_bounds.left, row_bounds.bottom + MulDiv(4, GetDpiForWindow(state.list), 96)};
    ClientToScreen(state.list, &point);
    SendMessageW(state.tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(point.x, point.y));
    SendMessageW(state.tooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
}

LRESULT CALLBACK list_owner_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR identifier, DWORD_PTR reference) {
    auto& state = *reinterpret_cast<ListState*>(reference);
    if (message == WM_NOTIFY) {
        const auto* notification = reinterpret_cast<NMHDR*>(lparam);
        if (notification->hwndFrom == state.list) {
            if (notification->code == LVN_GETEMPTYMARKUP && !state.empty_text.empty()) {
                auto* markup = reinterpret_cast<NMLVEMPTYMARKUP*>(lparam);
                markup->dwFlags = 0;
                wcsncpy_s(markup->szMarkup, state.empty_text.c_str(), _TRUNCATE);
                return TRUE;
            }
            if (notification->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lparam);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
                    return CDRF_NOTIFYSUBITEMDRAW | CDRF_NOTIFYPOSTPAINT;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT) {
                    RECT row{};
                    ListView_GetItemRect(state.list, static_cast<int>(draw->nmcd.dwItemSpec), &row, LVIR_BOUNDS);
                    draw_separator(draw->nmcd.hdc, row.left, row.right,
                        row.bottom - MulDiv(1, GetDpiForWindow(state.list), 96), GetDpiForWindow(state.list));
                    return CDRF_DODEFAULT;
                }
                if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const int row = static_cast<int>(draw->nmcd.dwItemSpec);
                    const auto text = cell_text(state.list, row, draw->iSubItem);
                    if (text.find(L'\\') == std::wstring::npos) return CDRF_DODEFAULT;
                    RECT bounds{};
                    ListView_GetSubItemRect(state.list, row, draw->iSubItem, LVIR_LABEL, &bounds);
                    const bool selected = ListView_GetItemState(state.list, row, LVIS_SELECTED) != 0;
                    FillRect(draw->nmcd.hdc, &bounds, selected
                        ? GetSysColorBrush(GetFocus() == state.list ? COLOR_HIGHLIGHT : COLOR_BTNFACE) : surface_brush());
                    if (draw->iSubItem == 0) {
                        LVITEMW item{.mask = LVIF_IMAGE, .iItem = row};
                        const auto images = ListView_GetImageList(state.list, LVSIL_SMALL);
                        if (images && ListView_GetItem(state.list, &item) && item.iImage >= 0) {
                            RECT icon{};
                            ListView_GetItemRect(state.list, row, &icon, LVIR_ICON);
                            FillRect(draw->nmcd.hdc, &icon, selected
                                ? GetSysColorBrush(GetFocus() == state.list ? COLOR_HIGHLIGHT : COLOR_BTNFACE)
                                : surface_brush());
                            int width = 0, height = 0;
                            ImageList_GetIconSize(images, &width, &height);
                            ImageList_Draw(images, item.iImage, draw->nmcd.hdc,
                                icon.left + std::max(0L, (icon.right - icon.left - width) / 2),
                                icon.top + std::max(0L, (icon.bottom - icon.top - height) / 2), ILD_TRANSPARENT);
                        }
                    }
                    SetTextColor(draw->nmcd.hdc, selected && GetFocus() == state.list
                        ? GetSysColor(COLOR_HIGHLIGHTTEXT) : text_color(state.list));
                    SetBkMode(draw->nmcd.hdc, TRANSPARENT);
                    bounds.left += MulDiv(12, GetDpiForWindow(state.list), 96);
                    bounds.right -= MulDiv(12, GetDpiForWindow(state.list), 96);
                    const auto displayed = compact_path_text(draw->nmcd.hdc, text, bounds.right - bounds.left);
                    DrawTextW(draw->nmcd.hdc, displayed.c_str(), -1, &bounds,
                        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                    return CDRF_SKIPDEFAULT;
                }
            }
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, list_owner_procedure, identifier);
    return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CALLBACK header_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR) {
    const auto result = DefSubclassProc(window, message, wparam, lparam);
    if (message == HDM_LAYOUT && result) {
        auto* layout = reinterpret_cast<HDLAYOUT*>(lparam);
        const auto height = MulDiv(32, GetDpiForWindow(window), 96);
        layout->prc->top += height - layout->pwpos->cy;
        layout->pwpos->cy = height;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, header_procedure, list_subclass);
    return result;
}

LRESULT CALLBACK list_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR reference) {
    auto& state = *reinterpret_cast<ListState*>(reference);
    if (message == WM_MOUSEMOVE) {
        hide_tip(state);
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_HOVER | TME_LEAVE, window, 500};
        TrackMouseEvent(&tracking);
    }
    if (message == WM_MOUSEHOVER) {
        POINT cursor{};
        GetCursorPos(&cursor);
        ScreenToClient(window, &cursor);
        LVHITTESTINFO hit{.pt = cursor};
        show_row_tip(state, ListView_SubItemHitTest(window, &hit));
    }
    if (message == WM_KEYUP && (wparam == VK_UP || wparam == VK_DOWN || wparam == VK_HOME
        || wparam == VK_END || wparam == VK_NEXT || wparam == VK_PRIOR || wparam == VK_F1))
        show_row_tip(state, ListView_GetNextItem(window, -1, LVNI_FOCUSED));
    if (message == WM_MOUSELEAVE || message == WM_KILLFOCUS || message == WM_LBUTTONDOWN
        || message == WM_MOUSEWHEEL || message == WM_VSCROLL || message == WM_HSCROLL
        || message == LVM_DELETEALLITEMS) hide_tip(state);
    if (message == WM_NOTIFY) {
        const auto* notify = reinterpret_cast<NMHDR*>(lparam);
        if (notify->hwndFrom == ListView_GetHeader(window) && notify->code == NM_CUSTOMDRAW
            && !high_contrast_enabled()) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lparam);
            if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                FillRect(draw->hdc, &draw->rc, brushes().navigation);
                wchar_t text[512]{};
                HDITEMW item{.mask = HDI_TEXT, .pszText = text, .cchTextMax = 512};
                Header_GetItem(notify->hwndFrom, draw->dwItemSpec, &item);
                auto bounds = draw->rc;
                bounds.left += MulDiv(12, GetDpiForWindow(window), 96);
                bounds.right -= MulDiv(12, GetDpiForWindow(window), 96);
                SetBkMode(draw->hdc, TRANSPARENT);
                SetTextColor(draw->hdc, RGB(96, 101, 109));
                const auto previous_font = state.header_font ? SelectObject(draw->hdc, state.header_font) : nullptr;
                DrawTextW(draw->hdc, text, -1, &bounds, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (previous_font) SelectObject(draw->hdc, previous_font);
                return CDRF_SKIPDEFAULT;
            }
        }
    }
    if (message == WM_SETFONT || message == WM_DPICHANGED_AFTERPARENT) {
        LOGFONTW font{};
        const auto body = reinterpret_cast<HFONT>(message == WM_SETFONT ? wparam : SendMessageW(window, WM_GETFONT, 0, 0));
        GetObjectW(body, sizeof(font), &font);
        font.lfHeight = -MulDiv(12, GetDpiForWindow(window), 96);
        font.lfWeight = FW_SEMIBOLD;
        const auto next_font = CreateFontIndirectW(&font);
        SendMessageW(ListView_GetHeader(window), WM_SETFONT, reinterpret_cast<WPARAM>(next_font), TRUE);
        if (state.header_font) DeleteObject(state.header_font);
        state.header_font = next_font;
        if (!ListView_GetImageList(window, LVSIL_SMALL)
            || ListView_GetImageList(window, LVSIL_SMALL) == state.spacing) {
            const auto next = ImageList_Create(1, MulDiv(36, GetDpiForWindow(window), 96) - 1, ILC_COLOR24, 1, 0);
            if (next) {
                ListView_SetImageList(window, next, LVSIL_SMALL);
                if (state.spacing) ImageList_Destroy(state.spacing);
                state.spacing = next;
            }
        }
    }
    if (message == WM_DESTROY && ListView_GetImageList(window, LVSIL_SMALL) == state.spacing)
        ListView_SetImageList(window, nullptr, LVSIL_SMALL);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(state.owner, list_owner_procedure, reinterpret_cast<UINT_PTR>(window));
        RemoveWindowSubclass(window, list_procedure, list_subclass);
        delete &state;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}
}

void style_list_view(HWND list) noexcept {
    ListView_SetBkColor(list, surface_color());
    ListView_SetTextBkColor(list, surface_color());
    ListView_SetTextColor(list, text_color(list));
    DWORD_PTR reference = 0;
    if (GetWindowSubclass(list, list_procedure, list_subclass, &reference)) return;
    auto state = std::unique_ptr<ListState>(new (std::nothrow) ListState);
    if (!state) return;
    state->owner = GetParent(list);
    state->list = list;
    if (!SetWindowSubclass(list, list_procedure, list_subclass,
        reinterpret_cast<DWORD_PTR>(state.get()))) return;
    SetWindowSubclass(state->owner, list_owner_procedure, reinterpret_cast<UINT_PTR>(list),
        reinterpret_cast<DWORD_PTR>(state.get()));
    state.release();
    SetWindowSubclass(ListView_GetHeader(list), header_procedure, list_subclass, 0);
}

void set_list_empty_text(HWND list, std::wstring_view text) {
    DWORD_PTR reference = 0;
    if (GetWindowSubclass(list, list_procedure, list_subclass, &reference))
        reinterpret_cast<ListState*>(reference)->empty_text = text;
    InvalidateRect(list, nullptr, FALSE);
}
} // namespace simpilot::settings_visual_style
