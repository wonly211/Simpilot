#pragma once

#include <Windows.h>

#include <optional>
#include <string>

namespace simpilot {

struct HotKeyGesture {
    UINT modifiers = 0;
    UINT virtual_key = 0;

    bool operator==(const HotKeyGesture&) const = default;

    [[nodiscard]] std::wstring display_text() const;
    [[nodiscard]] static bool is_modifier_key(UINT virtual_key) noexcept;
};

struct HotKeyBinding {
    std::optional<HotKeyGesture> gesture;
    bool force_override = false;

    bool operator==(const HotKeyBinding&) const = default;
};

[[nodiscard]] std::optional<std::size_t> windows_letter_hotkey_index(
    const HotKeyGesture& gesture) noexcept;
[[nodiscard]] bool is_supported_windows_letter_hotkey(
    const HotKeyGesture& gesture) noexcept;

} // namespace simpilot
