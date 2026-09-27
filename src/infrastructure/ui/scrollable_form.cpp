#include "scrollable_form.hpp"
#include "settings_visual_style.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace simpilot {
namespace { constexpr UINT_PTR form_subclass = 0x5351; }

ScrollableForm::~ScrollableForm() {
    if (viewport_) DestroyWindow(viewport_);
}

void ScrollableForm::attach(HWND owner, std::initializer_list<HWND> fixed) {
    if (viewport_) return;
    owner_ = owner;
    std::vector<HWND> children;
    for (auto child = GetWindow(owner, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        if (std::find(fixed.begin(), fixed.end(), child) == fixed.end()) children.push_back(child);
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    viewport_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL,
        0, 0, 0, 0, owner, nullptr, instance, nullptr);
    content_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, viewport_, nullptr, instance, nullptr);
    if (!viewport_ || !content_) throw std::runtime_error("Cannot create scrollable form");
    for (auto window : {viewport_, content_})
        SetWindowSubclass(window, procedure, form_subclass, reinterpret_cast<DWORD_PTR>(this));
    // SetParent inserts at the front; walk backwards to preserve Tab and label order.
    for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) {
        const auto child = *iterator;
        SetParent(child, content_);
        SetWindowSubclass(child, focus_procedure, form_subclass, reinterpret_cast<DWORD_PTR>(this));
    }
    SetWindowPos(viewport_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ScrollableForm::layout(int width, int height, int content_width, int content_height) {
    if (!viewport_) return;
    width_ = std::max(1, content_width);
    height_ = std::max(1, content_height);
    MoveWindow(viewport_, 0, 0, std::max(1, width), std::max(1, height), TRUE);
    arrange();
}

void ScrollableForm::arrange() {
    RECT client{};
    GetClientRect(viewport_, &client);
    const auto style = GetWindowLongPtrW(viewport_, GWL_STYLE);
    const auto dpi = GetDpiForWindow(viewport_);
    const int vertical_width = GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
    const int horizontal_height = GetSystemMetricsForDpi(SM_CYHSCROLL, dpi);
    const int available_width = client.right + ((style & WS_VSCROLL) ? vertical_width : 0);
    const int available_height = client.bottom + ((style & WS_HSCROLL) ? horizontal_height : 0);
    bool horizontal_needed = false, vertical_needed = false;
    // Solve from the unscrolled area so obsolete bars cannot keep each other alive.
    for (int pass = 0; pass < 3; ++pass) {
        horizontal_needed = width_ > available_width - (vertical_needed ? vertical_width : 0);
        vertical_needed = height_ > available_height - (horizontal_needed ? horizontal_height : 0);
    }
    ShowScrollBar(viewport_, SB_HORZ, horizontal_needed);
    ShowScrollBar(viewport_, SB_VERT, vertical_needed);
    GetClientRect(viewport_, &client);
    x_ = std::clamp(x_, 0, std::max(0, width_ - static_cast<int>(client.right)));
    y_ = std::clamp(y_, 0, std::max(0, height_ - static_cast<int>(client.bottom)));
    SCROLLINFO horizontal{sizeof(horizontal), SIF_RANGE | SIF_PAGE | SIF_POS,
        0, width_ - 1, static_cast<UINT>(client.right), x_};
    SCROLLINFO vertical{sizeof(vertical), SIF_RANGE | SIF_PAGE | SIF_POS,
        0, height_ - 1, static_cast<UINT>(client.bottom), y_};
    SetScrollInfo(viewport_, SB_HORZ, &horizontal, TRUE);
    SetScrollInfo(viewport_, SB_VERT, &vertical, TRUE);
    MoveWindow(content_, -x_, -y_, std::max(width_, static_cast<int>(client.right)),
        std::max(height_, static_cast<int>(client.bottom)), TRUE);
}

void ScrollableForm::reveal(HWND control) {
    if (!viewport_ || !IsChild(content_, control)) return;
    RECT rect{}, client{};
    GetWindowRect(control, &rect);
    MapWindowPoints(HWND_DESKTOP, viewport_, reinterpret_cast<POINT*>(&rect), 2);
    GetClientRect(viewport_, &client);
    if (rect.left < 0) x_ += rect.left;
    else if (rect.right > client.right) x_ += rect.right - client.right;
    if (rect.top < 0) y_ += rect.top;
    else if (rect.bottom > client.bottom) y_ += rect.bottom - client.bottom;
    arrange();
}

LRESULT CALLBACK ScrollableForm::focus_procedure(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR, DWORD_PTR reference) {
    if (message == WM_SETFOCUS) reinterpret_cast<ScrollableForm*>(reference)->reveal(window);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, focus_procedure, form_subclass);
    return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CALLBACK ScrollableForm::procedure(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR, DWORD_PTR reference) {
    auto& form = *reinterpret_cast<ScrollableForm*>(reference);
    if (message == WM_COMMAND || message == WM_NOTIFY
        || settings_visual_style::is_color_message(message))
        return SendMessageW(form.owner_, message, wparam, lparam);
    if (message == WM_ERASEBKGND) return settings_visual_style::erase_background(window, wparam);
    if (message == WM_VSCROLL || message == WM_HSCROLL || message == WM_MOUSEWHEEL) {
        const int step = MulDiv(36, GetDpiForWindow(window), 96);
        if (message == WM_MOUSEWHEEL) {
            form.wheel_ += GET_WHEEL_DELTA_WPARAM(wparam);
            form.y_ -= form.wheel_ / WHEEL_DELTA * step * 3;
            form.wheel_ %= WHEEL_DELTA;
        } else {
            const bool horizontal = message == WM_HSCROLL;
            auto& position = horizontal ? form.x_ : form.y_;
            SCROLLINFO info{sizeof(info), SIF_ALL};
            GetScrollInfo(form.viewport_, horizontal ? SB_HORZ : SB_VERT, &info);
            switch (LOWORD(wparam)) {
            case SB_TOP: position = 0; break;
            case SB_BOTTOM: position = info.nMax; break;
            case SB_LINEUP: position -= step; break;
            case SB_LINEDOWN: position += step; break;
            case SB_PAGEUP: position -= info.nPage; break;
            case SB_PAGEDOWN: position += info.nPage; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: position = info.nTrackPos; break;
            }
        }
        form.arrange();
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, procedure, form_subclass);
        if (window == form.viewport_) form.viewport_ = nullptr;
        if (window == form.content_) form.content_ = nullptr;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}
} // namespace simpilot
