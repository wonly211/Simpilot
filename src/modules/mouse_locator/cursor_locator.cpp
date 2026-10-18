#include "cursor_locator.hpp"

#include <shellscalingapi.h>

#include <algorithm>
#include <cstdint>

namespace simpilot {
namespace {

constexpr UINT_PTR sampling_timer = 1;
constexpr UINT sampling_interval_ms = 16;
constexpr auto highlight_duration = std::chrono::milliseconds(1250);
constexpr auto expansion_duration = std::chrono::milliseconds(180);
constexpr COLORREF transparent_color = RGB(1, 2, 3);

UINT dpi_for_point(const POINT point) noexcept {
    UINT horizontal = 96;
    UINT vertical = 96;
    const auto monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    if (monitor && SUCCEEDED(GetDpiForMonitor(
            monitor, MDT_EFFECTIVE_DPI, &horizontal, &vertical))) {
        return horizontal;
    }
    return GetDpiForSystem();
}

} // namespace

CursorLocator::CursorLocator(const HINSTANCE instance) noexcept
    : instance_(instance) {}

CursorLocator::~CursorLocator() {
    if (window_) {
        KillTimer(window_, sampling_timer);
        DestroyWindow(window_);
    }
}

bool CursorLocator::set_enabled(const bool enabled) noexcept {
    if (enabled == enabled_) return true;
    if (enabled) {
        if (!initialize()) return false;
        if (SetTimer(window_, sampling_timer, sampling_interval_ms, nullptr) == 0) {
            return false;
        }
        detector_.reset();
        enabled_ = true;
        return true;
    }

    enabled_ = false;
    detector_.reset();
    highlight_visible_ = false;
    if (window_) {
        KillTimer(window_, sampling_timer);
        ShowWindow(window_, SW_HIDE);
    }
    return true;
}

bool CursorLocator::enabled() const noexcept { return enabled_; }

bool CursorLocator::initialize() noexcept {
    if (window_) return true;
    const WNDCLASSW window_class{
        .lpfnWndProc = &CursorLocator::window_procedure,
        .hInstance = instance_,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .lpszClassName = cursor_locator_window_class_name,
    };
    if (!RegisterClassW(&window_class)
        && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE
            | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        cursor_locator_window_class_name, L"", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, instance_, this);
    if (!window_) return false;
    SetLayeredWindowAttributes(
        window_, transparent_color, 255, LWA_COLORKEY | LWA_ALPHA);
    return true;
}

void CursorLocator::handle_timer() noexcept {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    const auto now = MouseShakeDetector::Clock::now();
    if (detector_.update(cursor.x, cursor.y, now)) {
        show_highlight(cursor, now);
    }
    if (highlight_visible_) update_highlight(cursor, now);
}

void CursorLocator::show_highlight(
    const POINT cursor, const MouseShakeDetector::Clock::time_point now) noexcept {
    highlight_started_ = now;
    highlight_visible_ = true;
    update_highlight(cursor, now);
}

void CursorLocator::update_highlight(
    const POINT cursor, const MouseShakeDetector::Clock::time_point now) noexcept {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - highlight_started_);
    if (elapsed >= highlight_duration) {
        highlight_visible_ = false;
        ShowWindow(window_, SW_HIDE);
        return;
    }

    current_dpi_ = dpi_for_point(cursor);
    const auto expanded = std::min(elapsed, expansion_duration);
    const auto logical_diameter = 96
        + static_cast<int>(48 * expanded.count() / expansion_duration.count());
    const auto diameter = std::max(1, MulDiv(logical_diameter, current_dpi_, 96));
    const auto fade_started = std::chrono::milliseconds(180);
    const auto alpha = elapsed <= fade_started
        ? 235
        : std::max<std::int64_t>(0,
            235 * (highlight_duration - elapsed).count()
                / (highlight_duration - fade_started).count());

    SetLayeredWindowAttributes(window_, transparent_color,
        static_cast<BYTE>(alpha), LWA_COLORKEY | LWA_ALPHA);
    SetWindowPos(window_, HWND_TOPMOST,
        cursor.x - diameter / 2, cursor.y - diameter / 2,
        diameter, diameter,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);
}

void CursorLocator::paint(const HDC device) noexcept {
    RECT bounds{};
    GetClientRect(window_, &bounds);
    const auto background = CreateSolidBrush(transparent_color);
    FillRect(device, &bounds, background);
    DeleteObject(background);

    const auto inset = std::max(8, MulDiv(10, current_dpi_, 96));
    InflateRect(&bounds, -inset, -inset);
    const auto old_brush = SelectObject(device, GetStockObject(HOLLOW_BRUSH));
    const auto outline = CreatePen(
        PS_SOLID, std::max(3, MulDiv(10, current_dpi_, 96)), RGB(255, 255, 255));
    const auto old_pen = SelectObject(device, outline);
    Ellipse(device, bounds.left, bounds.top, bounds.right, bounds.bottom);
    const auto accent = CreatePen(
        PS_SOLID, std::max(2, MulDiv(5, current_dpi_, 96)), GetSysColor(COLOR_HIGHLIGHT));
    SelectObject(device, accent);
    Ellipse(device, bounds.left, bounds.top, bounds.right, bounds.bottom);
    SelectObject(device, old_pen);
    SelectObject(device, old_brush);
    DeleteObject(accent);
    DeleteObject(outline);
}

LRESULT CALLBACK CursorLocator::window_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* locator = static_cast<CursorLocator*>(creation->lpCreateParams);
        locator->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                         reinterpret_cast<LONG_PTR>(locator));
    }
    auto* locator = reinterpret_cast<CursorLocator*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    return locator ? locator->handle_message(message, wparam, lparam)
                   : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CursorLocator::handle_message(
    const UINT message, const WPARAM wparam, const LPARAM lparam) {
    switch (message) {
    case WM_TIMER:
        if (wparam == sampling_timer) {
            handle_timer();
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT painting{};
        const auto device = BeginPaint(window_, &painting);
        paint(device);
        EndPaint(window_, &painting);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_NCDESTROY: {
        const auto result = DefWindowProcW(window_, message, wparam, lparam);
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        window_ = nullptr;
        return result;
    }
    default:
        break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

} // namespace simpilot
