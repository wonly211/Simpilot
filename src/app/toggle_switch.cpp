#include "toggle_switch.hpp"

#include "settings_visual_style.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace simpilot::toggle_switch {
namespace {

constexpr UINT_PTR subclass_identifier = 1;

struct ControlState {
    bool draw_label = false;
    bool hot = false;
    bool pressed = false;
};

int scaled(const int value, const UINT dpi) noexcept {
    return MulDiv(value, dpi, 96);
}

void draw_glyph(const HDC dc, const RECT bounds, const bool checked,
                const bool enabled, const bool hot, const bool pressed,
                const UINT dpi) {
    const auto track_width = scaled(40, dpi);
    const auto track_height = scaled(20, dpi);
    RECT track{
        bounds.left + (bounds.right - bounds.left - track_width) / 2,
        bounds.top + (bounds.bottom - bounds.top - track_height) / 2,
        0, 0,
    };
    track.right = track.left + track_width;
    track.bottom = track.top + track_height;

    if (settings_visual_style::high_contrast_enabled()) {
        RECT checkbox{
            bounds.left + (bounds.right - bounds.left - scaled(16, dpi)) / 2,
            bounds.top + (bounds.bottom - bounds.top - scaled(16, dpi)) / 2,
            0, 0,
        };
        checkbox.right = checkbox.left + scaled(16, dpi);
        checkbox.bottom = checkbox.top + scaled(16, dpi);
        UINT state = DFCS_BUTTONCHECK;
        if (checked) state |= DFCS_CHECKED;
        if (!enabled) state |= DFCS_INACTIVE;
        DrawFrameControl(dc, &checkbox, DFC_BUTTON, state);
        return;
    }

    COLORREF fill = RGB(255, 255, 255);
    COLORREF border = RGB(118, 118, 118);
    COLORREF thumb = RGB(95, 95, 95);
    if (!enabled) {
        fill = checked ? RGB(166, 200, 225) : RGB(237, 237, 237);
        border = checked ? fill : RGB(200, 200, 200);
        thumb = checked ? RGB(255, 255, 255) : RGB(160, 160, 160);
    } else if (checked) {
        fill = pressed ? RGB(0, 78, 145) : hot ? RGB(0, 90, 158) : RGB(0, 103, 192);
        border = fill;
        thumb = RGB(255, 255, 255);
    } else if (pressed) {
        fill = RGB(224, 224, 224);
        thumb = RGB(48, 48, 48);
    } else if (hot) {
        fill = RGB(240, 240, 240);
        thumb = RGB(64, 64, 64);
    }

    const auto fill_brush = CreateSolidBrush(fill);
    const auto border_pen = CreatePen(PS_SOLID, std::max(1, scaled(1, dpi)), border);
    const auto old_brush = SelectObject(dc, fill_brush);
    const auto old_pen = SelectObject(dc, border_pen);
    RoundRect(dc, track.left, track.top, track.right, track.bottom,
              track_height, track_height);

    const auto thumb_size = scaled(16, dpi);
    const auto inset = scaled(2, dpi);
    const auto thumb_left = checked ? track.right - inset - thumb_size
                                    : track.left + inset;
    const auto thumb_brush = CreateSolidBrush(thumb);
    SelectObject(dc, thumb_brush);
    SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, thumb_left, track.top + inset,
            thumb_left + thumb_size, track.top + inset + thumb_size);

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(thumb_brush);
    DeleteObject(border_pen);
    DeleteObject(fill_brush);
}

void paint_control(const HWND window, const HDC dc, const ControlState& state) {
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, settings_visual_style::background_brush());
    const auto available_dpi = static_cast<UINT>(
        std::max(72L, (client.bottom - client.top) * 96L / 32L));
    const auto dpi = std::min(GetDpiForWindow(window), available_dpi);
    RECT glyph = client;
    if (state.draw_label) {
        glyph.left = std::max(client.left, client.right - scaled(48, dpi));
        RECT label = client;
        label.right = glyph.left - scaled(10, dpi);
        const auto length = GetWindowTextLengthW(window);
        std::wstring text(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
        GetWindowTextW(window, text.data(), length + 1);
        text.resize(static_cast<std::size_t>(std::max(0, length)));
        const auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        const auto old_font = font ? SelectObject(dc, font) : nullptr;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, settings_visual_style::text_color(window));
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &label,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (old_font) SelectObject(dc, old_font);
    }
    draw_glyph(dc, glyph,
        SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED,
        IsWindowEnabled(window) != FALSE, state.hot, state.pressed, dpi);
    if (GetFocus() == window) {
        RECT focus = state.draw_label ? client : glyph;
        InflateRect(&focus, -scaled(2, dpi), -scaled(2, dpi));
        DrawFocusRect(dc, &focus);
    }
}

LRESULT CALLBACK subclass_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam,
    const UINT_PTR, const DWORD_PTR reference_data) {
    auto* state = reinterpret_cast<ControlState*>(reference_data);
    if (!state) return DefSubclassProc(window, message, wparam, lparam);
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT painting{};
        const auto dc = BeginPaint(window, &painting);
        paint_control(window, dc, *state);
        EndPaint(window, &painting);
        return 0;
    }
    case WM_PRINTCLIENT:
        paint_control(window, reinterpret_cast<HDC>(wparam), *state);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (!state->hot) {
            state->hot = true;
            TRACKMOUSEEVENT tracking{
                .cbSize = sizeof(tracking),
                .dwFlags = TME_LEAVE,
                .hwndTrack = window,
            };
            TrackMouseEvent(&tracking);
            InvalidateRect(window, nullptr, TRUE);
        }
        break;
    case WM_MOUSELEAVE:
        state->hot = false;
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_LBUTTONDOWN:
        state->pressed = true;
        InvalidateRect(window, nullptr, TRUE);
        break;
    case WM_LBUTTONUP:
        state->pressed = false;
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case BM_SETCHECK: {
        const auto result = DefSubclassProc(window, message, wparam, lparam);
        InvalidateRect(window, nullptr, TRUE);
        return result;
    }
    case WM_NCDESTROY: {
        RemoveWindowSubclass(window, subclass_procedure, subclass_identifier);
        delete state;
        return DefSubclassProc(window, message, wparam, lparam);
    }
    default:
        break;
    }
    const auto result = DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_LBUTTONUP || message == WM_KEYUP) {
        InvalidateRect(window, nullptr, TRUE);
    }
    return result;
}

} // namespace

HWND create(const HINSTANCE instance, const HWND parent, const int identifier,
            const std::wstring_view accessible_name, const bool checked,
            const bool draw_label) {
    const std::wstring name(accessible_name);
    const auto control = CreateWindowW(L"BUTTON", name.c_str(),
        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)), instance, nullptr);
    if (!control) return nullptr;
    auto state = std::make_unique<ControlState>();
    state->draw_label = draw_label;
    if (!SetWindowSubclass(control, subclass_procedure, subclass_identifier,
                           reinterpret_cast<DWORD_PTR>(state.get()))) {
        DestroyWindow(control);
        return nullptr;
    }
    state.release();
    SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return control;
}

HIMAGELIST create_state_image_list(const UINT dpi) {
    const auto width = scaled(64, dpi);
    const auto height = scaled(32, dpi);
    const auto images = ImageList_Create(width, height, ILC_COLOR32 | ILC_MASK, 2, 0);
    if (!images) return nullptr;
    ImageList_SetBkColor(images, CLR_NONE);
    const auto screen = GetDC(nullptr);
    if (!screen) {
        ImageList_Destroy(images);
        return nullptr;
    }
    const auto bitmap = CreateCompatibleBitmap(screen, width, height);
    const auto memory = CreateCompatibleDC(screen);
    if (!bitmap || !memory) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        ImageList_Destroy(images);
        return nullptr;
    }
    const auto old_bitmap = SelectObject(memory, bitmap);
    constexpr COLORREF mask_color = RGB(255, 0, 255);
    const auto mask_brush = CreateSolidBrush(mask_color);
    const RECT canvas{0, 0, width, height};
    for (const bool checked : {false, true}) {
        FillRect(memory, &canvas, mask_brush);
        draw_glyph(memory, canvas, checked, true, false, false, dpi);
        ImageList_AddMasked(images, bitmap, mask_color);
    }
    SelectObject(memory, old_bitmap);
    DeleteObject(mask_brush);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return images;
}

} // namespace simpilot::toggle_switch
