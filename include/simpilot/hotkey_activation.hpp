#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace simpilot {

enum class ToggleRequirement { any, on, off };

struct HotkeyActivationConditions {
    // Caps Lock, Num Lock, Scroll Lock, in bit order.
    std::array<ToggleRequirement, 3> locks{};
    bool require_running_program = false;
    bool require_foreground_program = false;
    std::vector<std::wstring> programs;
    // Explicit precedence is required on both overlapping scenario rules.
    std::optional<unsigned> priority;

    bool operator==(const HotkeyActivationConditions&) const = default;
    [[nodiscard]] bool conditional() const noexcept;
    [[nodiscard]] std::uint8_t allowed_lock_states() const noexcept;
    [[nodiscard]] bool valid() const;
};

[[nodiscard]] std::wstring normalize_hotkey_program_path(std::wstring_view path);
[[nodiscard]] bool activation_overlaps(const HotkeyActivationConditions& left,
                                     const HotkeyActivationConditions& right);
[[nodiscard]] bool activation_conflicts(const HotkeyActivationConditions& left,
                                      const HotkeyActivationConditions& right);
class Localization;
[[nodiscard]] std::wstring activation_description(
    const HotkeyActivationConditions& conditions, const Localization& localization);

} // namespace simpilot
