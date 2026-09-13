#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace simpilot {

struct PhysicalKey {
    UINT virtual_key = 0;
    UINT scan_code = 0;
    bool extended = false;

    bool operator==(const PhysicalKey&) const = default;
    [[nodiscard]] bool empty() const noexcept { return virtual_key == 0; }
};

struct KeyboardTrigger {
    bool single_key = false;
    std::array<PhysicalKey, 4> modifiers{};
    std::size_t modifier_count = 0;
    PhysicalKey action{};
    std::optional<PhysicalKey> chord_action;

    bool operator==(const KeyboardTrigger&) const = default;
};

struct KeyboardOutput {
    bool single_key = false;
    std::array<PhysicalKey, 4> modifiers{};
    std::size_t modifier_count = 0;
    PhysicalKey action{};

    bool operator==(const KeyboardOutput&) const = default;
};

struct KeyboardMappingRule {
    KeyboardTrigger trigger{};
    KeyboardOutput output{};
    std::wstring process_name;
    bool exact_match = true;
    bool enabled = true;

    bool operator==(const KeyboardMappingRule&) const = default;
};

struct KeyboardMappingValidationError {
    std::size_t rule_index = 0;
    std::wstring message;
};

[[nodiscard]] bool is_mapping_modifier(UINT virtual_key) noexcept;
[[nodiscard]] bool is_mapping_physical_modifier(UINT virtual_key) noexcept;
[[nodiscard]] bool is_mapping_key_valid(const PhysicalKey& key) noexcept;
[[nodiscard]] std::wstring normalize_mapping_process_name(
    std::wstring value);
[[nodiscard]] std::wstring format_mapping_key(const PhysicalKey& key);
[[nodiscard]] std::wstring format_mapping_trigger(const KeyboardTrigger& trigger);
[[nodiscard]] std::wstring format_mapping_output(const KeyboardOutput& output);
[[nodiscard]] std::vector<KeyboardMappingValidationError>
validate_keyboard_mappings(const std::vector<KeyboardMappingRule>& rules);

} // namespace simpilot
