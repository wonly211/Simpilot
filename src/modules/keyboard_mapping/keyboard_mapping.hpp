#pragma once

#include "simpilot/keyboard_input.hpp"

namespace simpilot {

struct KeyboardMappingRule {
    KeyboardTrigger trigger{};
    KeyboardOutput output{};
    // Optional user-facing label shown in the settings mapping list.
    std::wstring purpose;
    // Empty means all applications; otherwise any listed foreground basename.
    std::vector<std::wstring> process_names;
    bool exact_match = true;
    bool enabled = true;

    bool operator==(const KeyboardMappingRule&) const = default;
};

inline constexpr std::size_t maximum_keyboard_mappings = 128;
inline constexpr std::size_t maximum_mapping_applications = 32;

[[nodiscard]] std::optional<std::vector<std::wstring>> normalize_mapping_process_names(
    const std::vector<std::wstring>& names);

enum class KeyboardMappingValidationKind {
    invalid, duplicate_source, modifier_conflict, prefix_conflict, cycle
};

struct KeyboardMappingValidationError {
    std::size_t rule_index = 0;
    std::wstring message;
    KeyboardMappingValidationKind kind = KeyboardMappingValidationKind::invalid;
    std::wstring process;
};

[[nodiscard]] std::vector<KeyboardMappingValidationError>
validate_keyboard_mappings(const std::vector<KeyboardMappingRule>& rules);

} // namespace simpilot
