#pragma once

#include "simpilot/hotkey.hpp"
#include "simpilot/localization.hpp"

#include <format>
#include <optional>
#include <string>

namespace simpilot {

inline std::wstring hotkey_capture_button_text(
    const std::optional<HotKeyGesture>& gesture, const bool capturing,
    const Localization& localization) {
    if (capturing) {
        return std::wstring(localization.text("hotkey_capture.recording"));
    }
    if (!gesture) {
        return std::wstring(localization.text("hotkey_capture.record"));
    }
    const auto display = gesture->display_text();
    return std::vformat(localization.text("hotkey_capture.record_again"),
                        std::make_wformat_args(display));
}

} // namespace simpilot
