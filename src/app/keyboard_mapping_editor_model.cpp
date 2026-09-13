#include "keyboard_mapping_editor_model.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <tuple>
#include <utility>

namespace simpilot {
namespace {

int modifier_rank(const PhysicalKey& key) noexcept {
    switch (key.virtual_key) {
    case VK_LCONTROL: return 0;
    case VK_RCONTROL: return 1;
    case VK_LMENU: return 2;
    case VK_RMENU: return 3;
    case VK_LSHIFT: return 4;
    case VK_RSHIFT: return 5;
    case VK_LWIN: return 6;
    case VK_RWIN: return 7;
    default: return 8;
    }
}

bool key_less(const PhysicalKey& left, const PhysicalKey& right) noexcept {
    return std::tuple(modifier_rank(left), left.virtual_key,
                      left.scan_code, left.extended)
        < std::tuple(modifier_rank(right), right.virtual_key,
                     right.scan_code, right.extended);
}

template <typename Keys>
bool contains_duplicate(const Keys& keys) noexcept {
    for (std::size_t left = 0; left < keys.size(); ++left) {
        if (!keys[left]) continue;
        for (std::size_t right = left + 1; right < keys.size(); ++right) {
            if (keys[right] && *keys[left] == *keys[right]) return true;
        }
    }
    return false;
}

template <typename Keys>
std::size_t append_modifiers(
    const Keys& source, std::array<PhysicalKey, 4>& target) noexcept {
    std::size_t count = 0;
    for (const auto& key : source) {
        if (key) target[count++] = *key;
    }
    std::ranges::sort(target.begin(),
                      target.begin() + static_cast<std::ptrdiff_t>(count),
                      key_less);
    return count;
}

template <typename Keys>
bool modifiers_are_valid(const Keys& keys) noexcept {
    return std::ranges::all_of(keys, [](const auto& key) {
        return !key || (is_mapping_key_valid(*key)
            && is_mapping_modifier(key->virtual_key));
    });
}

bool catalog_extended_key(const UINT virtual_key) noexcept {
    switch (virtual_key) {
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_SNAPSHOT:
    case VK_BROWSER_BACK:
    case VK_BROWSER_FORWARD:
    case VK_BROWSER_REFRESH:
    case VK_BROWSER_STOP:
    case VK_BROWSER_SEARCH:
    case VK_BROWSER_FAVORITES:
    case VK_BROWSER_HOME:
    case VK_VOLUME_MUTE:
    case VK_VOLUME_DOWN:
    case VK_VOLUME_UP:
    case VK_MEDIA_NEXT_TRACK:
    case VK_MEDIA_PREV_TRACK:
    case VK_MEDIA_STOP:
    case VK_MEDIA_PLAY_PAUSE:
    case VK_LAUNCH_MAIL:
    case VK_LAUNCH_MEDIA_SELECT:
    case VK_LAUNCH_APP1:
    case VK_LAUNCH_APP2:
        return true;
    default:
        return false;
    }
}

} // namespace

KeyboardMappingEditorModel::KeyboardMappingEditorModel(
    const KeyboardMappingRule& rule) noexcept {
    set_trigger(rule.trigger);
    set_output(rule.output);
}

void KeyboardMappingEditorModel::set_trigger(
    const KeyboardTrigger& trigger) noexcept {
    source_modifiers_.fill(std::nullopt);
    const auto count = std::min(trigger.modifier_count,
                                source_modifiers_.size());
    for (std::size_t index = 0; index < count; ++index) {
        source_modifiers_[index] = trigger.modifiers[index];
    }
    source_action_ = trigger.action.empty()
        ? std::nullopt : std::optional(trigger.action);
    source_chord_action_ = trigger.chord_action;
}

void KeyboardMappingEditorModel::set_output(
    const KeyboardOutput& output) noexcept {
    target_modifiers_.fill(std::nullopt);
    const auto count = std::min(output.modifier_count,
                                target_modifiers_.size());
    for (std::size_t index = 0; index < count; ++index) {
        target_modifiers_[index] = output.modifiers[index];
    }
    target_action_ = output.action.empty()
        ? std::nullopt : std::optional(output.action);
}

void KeyboardMappingEditorModel::set_source_modifier(
    const std::size_t index, std::optional<PhysicalKey> key) noexcept {
    if (index < source_modifiers_.size()) {
        source_modifiers_[index] = std::move(key);
    }
}

void KeyboardMappingEditorModel::set_target_modifier(
    const std::size_t index, std::optional<PhysicalKey> key) noexcept {
    if (index < target_modifiers_.size()) {
        target_modifiers_[index] = std::move(key);
    }
}

void KeyboardMappingEditorModel::set_source_action(
    std::optional<PhysicalKey> key) noexcept {
    source_action_ = std::move(key);
}

void KeyboardMappingEditorModel::set_source_chord_action(
    std::optional<PhysicalKey> key) noexcept {
    source_chord_action_ = std::move(key);
}

void KeyboardMappingEditorModel::set_target_action(
    std::optional<PhysicalKey> key) noexcept {
    target_action_ = std::move(key);
}

const std::array<std::optional<PhysicalKey>, 4>&
KeyboardMappingEditorModel::source_modifiers() const noexcept {
    return source_modifiers_;
}

const std::array<std::optional<PhysicalKey>, 4>&
KeyboardMappingEditorModel::target_modifiers() const noexcept {
    return target_modifiers_;
}

const std::optional<PhysicalKey>&
KeyboardMappingEditorModel::source_action() const noexcept {
    return source_action_;
}

const std::optional<PhysicalKey>&
KeyboardMappingEditorModel::source_chord_action() const noexcept {
    return source_chord_action_;
}

const std::optional<PhysicalKey>&
KeyboardMappingEditorModel::target_action() const noexcept {
    return target_action_;
}

KeyboardMappingDraftResult KeyboardMappingEditorModel::build() const noexcept {
    KeyboardMappingDraftResult result;
    if (!modifiers_are_valid(source_modifiers_)) {
        result.error = KeyboardMappingDraftError::source_modifier_invalid;
        return result;
    }
    if (!modifiers_are_valid(target_modifiers_)) {
        result.error = KeyboardMappingDraftError::target_modifier_invalid;
        return result;
    }
    if (contains_duplicate(source_modifiers_)) {
        result.error = KeyboardMappingDraftError::source_modifier_duplicate;
        return result;
    }
    if (contains_duplicate(target_modifiers_)) {
        result.error = KeyboardMappingDraftError::target_modifier_duplicate;
        return result;
    }
    if (!source_action_) {
        result.error = KeyboardMappingDraftError::source_action_required;
        return result;
    }
    if (!target_action_) {
        result.error = KeyboardMappingDraftError::target_action_required;
        return result;
    }
    if (!is_mapping_key_valid(*source_action_)
        || is_mapping_modifier(source_action_->virtual_key)) {
        result.error = KeyboardMappingDraftError::source_action_invalid;
        return result;
    }
    if (!is_mapping_key_valid(*target_action_)
        || is_mapping_modifier(target_action_->virtual_key)) {
        result.error = KeyboardMappingDraftError::target_action_invalid;
        return result;
    }
    if (source_chord_action_
        && (!is_mapping_key_valid(*source_chord_action_)
            || is_mapping_modifier(source_chord_action_->virtual_key)
            || *source_chord_action_ == *source_action_)) {
        result.error = KeyboardMappingDraftError::source_chord_invalid;
        return result;
    }

    result.trigger.modifier_count = append_modifiers(
        source_modifiers_, result.trigger.modifiers);
    if (source_chord_action_ && result.trigger.modifier_count > 3) {
        result.error = KeyboardMappingDraftError::source_chord_modifier_limit;
        return result;
    }
    result.trigger.action = *source_action_;
    result.trigger.chord_action = source_chord_action_;
    result.trigger.single_key = result.trigger.modifier_count == 0
        && !result.trigger.chord_action;

    result.output.modifier_count = append_modifiers(
        target_modifiers_, result.output.modifiers);
    result.output.action = *target_action_;
    result.output.single_key = result.output.modifier_count == 0;
    return result;
}

PhysicalKey keyboard_mapping_catalog_key(
    const UINT virtual_key, const bool extended_hint) noexcept {
    const auto layout = GetKeyboardLayout(0);
    auto mapped = MapVirtualKeyExW(
        virtual_key, MAPVK_VK_TO_VSC_EX, layout);
    if (mapped == 0) {
        mapped = MapVirtualKeyExW(virtual_key, MAPVK_VK_TO_VSC, layout);
    }
    const auto prefix = mapped & 0xFF00U;
    return {
        .virtual_key = virtual_key,
        .scan_code = mapped & 0x00FFU,
        .extended = extended_hint || catalog_extended_key(virtual_key)
            || prefix == 0xE000U,
    };
}

std::array<PhysicalKey, 8> keyboard_mapping_modifier_catalog() noexcept {
    return {
        keyboard_mapping_catalog_key(VK_LCONTROL),
        keyboard_mapping_catalog_key(VK_RCONTROL),
        keyboard_mapping_catalog_key(VK_LMENU),
        keyboard_mapping_catalog_key(VK_RMENU),
        keyboard_mapping_catalog_key(VK_LSHIFT),
        keyboard_mapping_catalog_key(VK_RSHIFT),
        keyboard_mapping_catalog_key(VK_LWIN),
        keyboard_mapping_catalog_key(VK_RWIN),
    };
}

std::vector<PhysicalKey> keyboard_mapping_action_catalog() {
    std::vector<PhysicalKey> result;
    const auto append = [&result](const UINT virtual_key,
                                  const bool extended = false) {
        const auto key = keyboard_mapping_catalog_key(virtual_key, extended);
        if (key.scan_code == 0 || std::ranges::find(result, key) != result.end()) {
            return;
        }
        result.push_back(key);
    };

    for (auto key = static_cast<UINT>(L'A'); key <= static_cast<UINT>(L'Z'); ++key) {
        append(key);
    }
    for (auto key = static_cast<UINT>(L'0'); key <= static_cast<UINT>(L'9'); ++key) {
        append(key);
    }
    for (auto key = static_cast<UINT>(VK_F1); key <= static_cast<UINT>(VK_F24); ++key) {
        append(key);
    }

    constexpr std::array common_keys{
        VK_ESCAPE, VK_TAB, VK_CAPITAL, VK_BACK, VK_RETURN, VK_SPACE,
        VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
        VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_SNAPSHOT, VK_SCROLL,
        VK_PAUSE, VK_APPS,
    };
    for (const auto key : common_keys) append(key);
    append(VK_RETURN, true); // Numpad Enter.

    for (auto key = static_cast<UINT>(VK_NUMPAD0);
         key <= static_cast<UINT>(VK_NUMPAD9); ++key) {
        append(key);
    }
    constexpr std::array numpad_keys{
        VK_DECIMAL, VK_DIVIDE, VK_MULTIPLY, VK_SUBTRACT, VK_ADD,
    };
    for (const auto key : numpad_keys) append(key);

    constexpr std::array oem_keys{
        VK_OEM_1, VK_OEM_PLUS, VK_OEM_COMMA, VK_OEM_MINUS,
        VK_OEM_PERIOD, VK_OEM_2, VK_OEM_3, VK_OEM_4, VK_OEM_5,
        VK_OEM_6, VK_OEM_7, VK_OEM_8, VK_OEM_102,
    };
    for (const auto key : oem_keys) append(key);

    constexpr std::array media_keys{
        VK_BROWSER_BACK, VK_BROWSER_FORWARD, VK_BROWSER_REFRESH,
        VK_BROWSER_STOP, VK_BROWSER_SEARCH, VK_BROWSER_FAVORITES,
        VK_BROWSER_HOME, VK_VOLUME_MUTE, VK_VOLUME_DOWN, VK_VOLUME_UP,
        VK_MEDIA_NEXT_TRACK, VK_MEDIA_PREV_TRACK, VK_MEDIA_STOP,
        VK_MEDIA_PLAY_PAUSE, VK_LAUNCH_MAIL, VK_LAUNCH_MEDIA_SELECT,
        VK_LAUNCH_APP1, VK_LAUNCH_APP2,
    };
    for (const auto key : media_keys) append(key);
    return result;
}

} // namespace simpilot
