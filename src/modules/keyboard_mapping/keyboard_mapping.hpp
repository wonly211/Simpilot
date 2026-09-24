#pragma once

#include "simpilot/keyboard_input.hpp"

namespace simpilot {

struct KeyboardMappingRule {
    KeyboardTrigger trigger{};
    KeyboardOutput output{};
    // Optional user-facing label shown in the settings mapping list.
    std::wstring purpose;
    std::wstring process_name;
    bool exact_match = true;
    bool enabled = true;

    bool operator==(const KeyboardMappingRule&) const = default;
};

struct KeyboardMappingValidationError {
    std::size_t rule_index = 0;
    std::wstring message;
};

[[nodiscard]] std::vector<KeyboardMappingValidationError>
validate_keyboard_mappings(const std::vector<KeyboardMappingRule>& rules);

} // namespace simpilot
