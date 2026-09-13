#pragma once

#include "simpilot/keyboard_mapping.hpp"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace simpilot {

enum class KeyboardMappingDraftError {
    none,
    source_action_required,
    target_action_required,
    source_action_invalid,
    target_action_invalid,
    source_modifier_invalid,
    target_modifier_invalid,
    source_modifier_duplicate,
    target_modifier_duplicate,
    source_chord_invalid,
    source_chord_modifier_limit,
};

struct KeyboardMappingDraftResult {
    KeyboardMappingDraftError error = KeyboardMappingDraftError::none;
    KeyboardTrigger trigger;
    KeyboardOutput output;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == KeyboardMappingDraftError::none;
    }
};

class KeyboardMappingEditorModel final {
public:
    KeyboardMappingEditorModel() = default;
    explicit KeyboardMappingEditorModel(const KeyboardMappingRule& rule) noexcept;

    void set_trigger(const KeyboardTrigger& trigger) noexcept;
    void set_output(const KeyboardOutput& output) noexcept;
    void set_source_modifier(
        std::size_t index, std::optional<PhysicalKey> key) noexcept;
    void set_target_modifier(
        std::size_t index, std::optional<PhysicalKey> key) noexcept;
    void set_source_action(std::optional<PhysicalKey> key) noexcept;
    void set_source_chord_action(std::optional<PhysicalKey> key) noexcept;
    void set_target_action(std::optional<PhysicalKey> key) noexcept;

    [[nodiscard]] const std::array<std::optional<PhysicalKey>, 4>&
    source_modifiers() const noexcept;
    [[nodiscard]] const std::array<std::optional<PhysicalKey>, 4>&
    target_modifiers() const noexcept;
    [[nodiscard]] const std::optional<PhysicalKey>& source_action() const noexcept;
    [[nodiscard]] const std::optional<PhysicalKey>&
    source_chord_action() const noexcept;
    [[nodiscard]] const std::optional<PhysicalKey>& target_action() const noexcept;
    [[nodiscard]] KeyboardMappingDraftResult build() const noexcept;

private:
    std::array<std::optional<PhysicalKey>, 4> source_modifiers_{};
    std::array<std::optional<PhysicalKey>, 4> target_modifiers_{};
    std::optional<PhysicalKey> source_action_;
    std::optional<PhysicalKey> source_chord_action_;
    std::optional<PhysicalKey> target_action_;
};

// Produces the same low-byte scan-code plus extended-bit representation as
// KBDLLHOOKSTRUCT. The optional hint distinguishes physical variants such as
// main Enter and numpad Enter that share one virtual-key value.
[[nodiscard]] PhysicalKey keyboard_mapping_catalog_key(
    UINT virtual_key, bool extended_hint = false) noexcept;
[[nodiscard]] std::array<PhysicalKey, 8>
keyboard_mapping_modifier_catalog() noexcept;
[[nodiscard]] std::vector<PhysicalKey> keyboard_mapping_action_catalog();

} // namespace simpilot
