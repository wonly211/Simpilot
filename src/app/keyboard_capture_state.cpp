#include "keyboard_capture_state.hpp"

#include <algorithm>
#include <limits>
#include <ranges>

namespace simpilot {
namespace {

constexpr std::size_t no_index = std::numeric_limits<std::size_t>::max();

template <typename Candidate>
bool contains_modifier(
    const Candidate& candidate, const PhysicalKey& key) noexcept {
    return std::ranges::any_of(
        candidate.modifiers.begin(),
        candidate.modifiers.begin() + static_cast<std::ptrdiff_t>(candidate.modifier_count),
        [&key](const PhysicalKey& current) { return current == key; });
}

template <typename Candidate>
void erase_modifier(Candidate& candidate, const PhysicalKey& key) noexcept {
    const auto end = candidate.modifiers.begin()
        + static_cast<std::ptrdiff_t>(candidate.modifier_count);
    const auto found = std::ranges::find_if(
        candidate.modifiers.begin(), end,
        [&key](const PhysicalKey& current) {
            return current == key || current.virtual_key == key.virtual_key;
        });
    if (found == end) return;
    std::move(found + 1, end, found);
    --candidate.modifier_count;
    candidate.modifiers[candidate.modifier_count] = {};
}

} // namespace

void KeyboardCaptureState::begin(const CaptureMode mode) noexcept {
    // A new begin is a new generation.  Clearing the old completion is
    // important when a dialog changes fields while the previous capture is
    // still unwinding through the keyboard thread.
    reset();
    session_ = session_ == std::numeric_limits<std::uint64_t>::max()
        ? 1
        : session_ + 1;
    mode_ = mode;
    active_ = true;
}

void KeyboardCaptureState::end() noexcept {
    reset();
}

void KeyboardCaptureState::reset() noexcept {
    active_ = false;
    mode_ = CaptureMode::legacy;
    pressed_.fill({});
    pressed_count_ = 0;
    legacy_candidate_ = {};
    trigger_candidate_ = {};
    output_candidate_ = {};
    standalone_modifier_candidate_.reset();
    has_action_ = false;
    invalid_candidate_ = false;
    live_modifier_mask_ = 0;
    live_modifiers_.fill({});
    live_modifier_count_ = 0;
    completed_.reset();
}

bool KeyboardCaptureState::is_key_message(const UINT message) noexcept {
    return is_key_down_message(message) || is_key_up_message(message);
}

bool KeyboardCaptureState::is_key_down_message(const UINT message) noexcept {
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

bool KeyboardCaptureState::is_key_up_message(const UINT message) noexcept {
    return message == WM_KEYUP || message == WM_SYSKEYUP;
}

bool KeyboardCaptureState::is_escape(const PhysicalKey& key) noexcept {
    return key.virtual_key == VK_ESCAPE;
}

bool KeyboardCaptureState::is_modifier(const PhysicalKey& key) noexcept {
    return HotKeyGesture::is_modifier_key(key.virtual_key);
}

UINT KeyboardCaptureState::modifier_mask(const PhysicalKey& key) noexcept {
    switch (key.virtual_key) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return MOD_CONTROL;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return MOD_ALT;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return MOD_SHIFT;
    case VK_LWIN:
    case VK_RWIN:
        return MOD_WIN;
    default:
        return 0;
    }
}

std::size_t KeyboardCaptureState::find_pressed(
    const PhysicalKey& key) const noexcept {
    for (std::size_t index = 0; index < pressed_count_; ++index) {
        if (pressed_[index].key == key) return index;
    }
    // Some producers do not preserve the scan code in synthetic key-up
    // messages.  Keep physical identity strict unless this event actually
    // lacks scan information.
    if (key.scan_code == 0) {
        for (std::size_t index = 0; index < pressed_count_; ++index) {
            if (pressed_[index].key.virtual_key == key.virtual_key) return index;
        }
    }
    return no_index;
}

bool KeyboardCaptureState::add_pressed(const PhysicalKey& key) noexcept {
    if (pressed_count_ >= pressed_.size()) return false;
    pressed_[pressed_count_] = {.key = key, .down = true};
    ++pressed_count_;
    return true;
}

void KeyboardCaptureState::remove_pressed(const std::size_t index) noexcept {
    if (index >= pressed_count_) return;
    const auto last = pressed_count_ - 1;
    if (index != last) pressed_[index] = pressed_[last];
    pressed_[last] = {};
    --pressed_count_;
}

bool KeyboardCaptureState::all_released() const noexcept {
    return pressed_count_ == 0;
}

bool KeyboardCaptureState::already_captured(
    const PhysicalKey& key) const noexcept {
    if (is_modifier(key)) {
        if (mode_ == CaptureMode::legacy) {
            return (legacy_candidate_.modifiers & modifier_mask(key)) != 0;
        }
        if (mode_ == CaptureMode::mapping_trigger) {
            return contains_modifier(trigger_candidate_, key);
        }
        return contains_modifier(output_candidate_, key);
    }

    if (mode_ == CaptureMode::legacy) {
        return has_action_ && legacy_candidate_.virtual_key == key.virtual_key;
    }
    if (mode_ == CaptureMode::mapping_trigger) {
        return trigger_candidate_.action == key
            || (trigger_candidate_.chord_action
                && *trigger_candidate_.chord_action == key);
    }
    return output_candidate_.action == key;
}

bool KeyboardCaptureState::add_modifier(const PhysicalKey& key) noexcept {
    if (mode_ == CaptureMode::legacy) {
        legacy_candidate_.modifiers |= modifier_mask(key);
        return true;
    }

    if (mode_ == CaptureMode::mapping_trigger) {
        if (contains_modifier(trigger_candidate_, key)) return true;
        if (trigger_candidate_.modifier_count >= trigger_candidate_.modifiers.size()) {
            return false;
        }
        trigger_candidate_.modifiers[trigger_candidate_.modifier_count++] = key;
        return true;
    }
    if (contains_modifier(output_candidate_, key)) return true;
    if (output_candidate_.modifier_count >= output_candidate_.modifiers.size()) {
        return false;
    }
    output_candidate_.modifiers[output_candidate_.modifier_count++] = key;
    return true;
}

bool KeyboardCaptureState::add_action(const PhysicalKey& key) noexcept {
    if (mode_ == CaptureMode::legacy) {
        // This follows the legacy recorder's "last action wins" behavior,
        // while repeated key-down messages for the same key are ignored by
        // already_captured().
        legacy_candidate_.virtual_key = key.virtual_key;
        has_action_ = true;
        return true;
    }

    if (mode_ == CaptureMode::mapping_trigger) {
        if (trigger_candidate_.action.empty()) {
            trigger_candidate_.action = key;
            has_action_ = true;
            return true;
        }
        if (trigger_candidate_.action == key) return true;
        if (trigger_candidate_.chord_action) {
            return *trigger_candidate_.chord_action == key;
        }
        // A two-action trigger is only valid with at most three modifiers.
        if (trigger_candidate_.modifier_count > 3) return false;
        trigger_candidate_.chord_action = key;
        has_action_ = true;
        return true;
    }

    if (output_candidate_.action.empty()) {
        output_candidate_.action = key;
        has_action_ = true;
        return true;
    }
    return output_candidate_.action == key;
}

void KeyboardCaptureState::remove_modifier(const PhysicalKey& key) noexcept {
    if (mode_ == CaptureMode::legacy) {
        legacy_candidate_.modifiers &= ~modifier_mask(key);
        return;
    }
    if (mode_ == CaptureMode::mapping_trigger) {
        erase_modifier(trigger_candidate_, key);
    } else {
        erase_modifier(output_candidate_, key);
    }
}

CaptureCompletion KeyboardCaptureState::make_completion() const noexcept {
    CaptureCompletion completion{
        .mode = mode_,
        .session = session_,
        .legacy_gesture = legacy_candidate_,
        .trigger = trigger_candidate_,
        .output = output_candidate_,
    };
    if (mode_ == CaptureMode::mapping_trigger) {
        completion.trigger.single_key = completion.trigger.modifier_count == 0
            && !completion.trigger.chord_action;
    } else if (mode_ == CaptureMode::mapping_output) {
        completion.output.single_key = completion.output.modifier_count == 0;
    }
    return completion;
}

CaptureEventResult KeyboardCaptureState::complete() noexcept {
    const auto completion = make_completion();
    completed_ = completion;

    CaptureEventResult result{
        .mode = mode_,
        .suppress = true,
        .completed = true,
        .cancelled = false,
        .legacy_gesture = completion.legacy_gesture,
        .trigger = completion.trigger,
        .output = completion.output,
    };
    active_ = false;
    pressed_.fill({});
    pressed_count_ = 0;
    legacy_candidate_ = {};
    trigger_candidate_ = {};
    output_candidate_ = {};
    standalone_modifier_candidate_.reset();
    has_action_ = false;
    invalid_candidate_ = false;
    live_modifier_mask_ = 0;
    live_modifiers_.fill({});
    live_modifier_count_ = 0;
    return result;
}

CaptureEventResult KeyboardCaptureState::cancel() noexcept {
    CaptureEventResult result{
        .mode = mode_,
        .suppress = true,
        .completed = false,
        .cancelled = true,
    };
    active_ = false;
    pressed_.fill({});
    pressed_count_ = 0;
    legacy_candidate_ = {};
    trigger_candidate_ = {};
    output_candidate_ = {};
    standalone_modifier_candidate_.reset();
    has_action_ = false;
    invalid_candidate_ = false;
    live_modifier_mask_ = 0;
    live_modifiers_.fill({});
    live_modifier_count_ = 0;
    completed_.reset();
    return result;
}

CaptureEventResult KeyboardCaptureState::handle(
    const UINT message, const PhysicalKey& key) noexcept {
    CaptureEventResult result{};
    if (!active_ || !is_key_message(message)) return result;

    // The hook can return this decision immediately.  All state transitions
    // below are bounded in size and perform no Windows calls or waits.
    result.suppress = true;

    if (is_key_down_message(message)) {
        const auto modifier_is_down = std::ranges::any_of(
            pressed_.begin(), pressed_.begin() + static_cast<std::ptrdiff_t>(pressed_count_),
            [this](const PressedKey& pressed) {
                return pressed.down && is_modifier(pressed.key);
            });
        if (mode_ == CaptureMode::legacy && is_escape(key) && !modifier_is_down) {
            return cancel();
        }
        if (key.empty()) return result;

        const auto pressed_index = find_pressed(key);
        if (pressed_index != no_index) return result;
        if (!add_pressed(key)) return cancel();
        if (already_captured(key)) return result;

        if (mode_ == CaptureMode::mapping_trigger) {
            if (is_modifier(key) && pressed_count_ == 1 && !has_action_
                && trigger_candidate_.modifier_count == 0) {
                standalone_modifier_candidate_ = key;
            } else {
                standalone_modifier_candidate_.reset();
            }
        }

        const auto accepted = is_modifier(key)
            ? add_modifier(key) : add_action(key);
        if (!accepted) return cancel();
        return result;
    }

    const auto pressed_index = find_pressed(key);
    if (pressed_index != no_index) remove_pressed(pressed_index);

    if (mode_ == CaptureMode::mapping_trigger && !has_action_
        && all_released() && standalone_modifier_candidate_
        && trigger_candidate_.modifier_count == 1) {
        trigger_candidate_.action = *standalone_modifier_candidate_;
        trigger_candidate_.modifiers[0] = {};
        trigger_candidate_.modifier_count = 0;
        standalone_modifier_candidate_.reset();
        has_action_ = true;
        return complete();
    }

    // A modifier released before an action is pressed is not part of the
    // gesture.  Once an action exists, retain all candidates until the final
    // release so either release order yields the same shortcut.
    if (!has_action_ && is_modifier(key)) remove_modifier(key);

    if (all_released() && has_action_) return complete();
    return result;
}

std::optional<CaptureCompletion> KeyboardCaptureState::take_completed() noexcept {
    if (!completed_) return std::nullopt;
    auto result = completed_;
    completed_.reset();
    return result;
}

} // namespace simpilot
