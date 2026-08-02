#pragma once

#include "simpilot/hotkey.hpp"
#include "simpilot/localization.hpp"

#include <format>
#include <optional>
#include <string>

namespace simpilot {

inline std::wstring hotkey_capture_button_text(
    const std::optional<HotKeyGesture>& gesture, const bool capturing,
    const UiLanguage language) {
    if (capturing) {
        return language == UiLanguage::simplified_chinese
            ? L"正在录制…（Esc 取消）"
            : L"Recording… (Esc to cancel)";
    }
    if (!gesture) {
        return language == UiLanguage::simplified_chinese
            ? L"录制热键"
            : L"Record hotkey";
    }
    return language == UiLanguage::simplified_chinese
        ? std::format(L"{}  ·  重新录制", gesture->display_text())
        : std::format(L"{}  ·  Record again", gesture->display_text());
}

} // namespace simpilot
