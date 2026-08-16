#pragma once

#include "simpilot/mouse_shake_detector.hpp"

#include <Windows.h>

#include <chrono>

namespace simpilot {

inline constexpr wchar_t cursor_locator_window_class_name[] =
    L"Simpilot.CursorLocator";

class CursorLocator final {
public:
    explicit CursorLocator(HINSTANCE instance) noexcept;
    ~CursorLocator();

    CursorLocator(const CursorLocator&) = delete;
    CursorLocator& operator=(const CursorLocator&) = delete;

    [[nodiscard]] bool set_enabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;

private:
    [[nodiscard]] bool initialize() noexcept;
    void handle_timer() noexcept;
    void show_highlight(POINT cursor,
                        MouseShakeDetector::Clock::time_point now) noexcept;
    void update_highlight(POINT cursor,
                          MouseShakeDetector::Clock::time_point now) noexcept;
    void paint(HDC device) noexcept;

    static LRESULT CALLBACK window_procedure(HWND window, UINT message,
                                             WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    MouseShakeDetector detector_;
    bool enabled_ = false;
    bool highlight_visible_ = false;
    UINT current_dpi_ = 96;
    MouseShakeDetector::Clock::time_point highlight_started_{};
};

} // namespace simpilot
