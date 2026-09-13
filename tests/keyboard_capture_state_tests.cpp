#include "keyboard_capture_state.hpp"

#include <Windows.h>

#include <iostream>
#include <optional>
#include <stdexcept>

namespace {

using simpilot::CaptureCompletion;
using simpilot::CaptureMode;
using simpilot::KeyboardCaptureState;
using simpilot::PhysicalKey;

PhysicalKey physical(
    const UINT virtual_key, const UINT scan_code = 0,
    const bool extended = false) noexcept {
    return {.virtual_key = virtual_key,
            .scan_code = scan_code,
            .extended = extended};
}

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_suppressed(
    const simpilot::CaptureEventResult& result, const char* message) {
    require(result.suppress && !result.cancelled, message);
}

void capture_legacy_key(
    KeyboardCaptureState& state, const PhysicalKey& key,
    const UINT down_message = WM_KEYDOWN,
    const UINT up_message = WM_KEYUP) {
    require_suppressed(state.handle(down_message, key),
        "Legacy key down must be suppressed while capturing");
    const auto result = state.handle(up_message, key);
    require_suppressed(result,
        "Legacy key up must be suppressed while capturing");
    require(result.completed && !state.active(),
        "A released action key must complete the capture");
}

void captures_all_keyboard_hook_message_variants() {
    KeyboardCaptureState state;
    state.begin(CaptureMode::legacy);

    const auto control = physical(VK_LCONTROL, 0x1D);
    const auto action = physical(L'A', 0x1E);
    require_suppressed(state.handle(WM_KEYDOWN, control),
        "WM_KEYDOWN must be suppressed");
    require_suppressed(state.handle(WM_SYSKEYDOWN, action),
        "WM_SYSKEYDOWN must be suppressed");
    require_suppressed(state.handle(WM_SYSKEYUP, action),
        "WM_SYSKEYUP must be suppressed");
    const auto result = state.handle(WM_KEYUP, control);
    require_suppressed(result, "WM_KEYUP must be suppressed");
    require(result.completed, "All four hook message kinds must complete capture");

    const auto completion = state.take_completed();
    require(completion.has_value(), "Completed capture must be available");
    require(completion->legacy_gesture == simpilot::HotKeyGesture{MOD_CONTROL, L'A'},
        "Hook message variants must preserve the Ctrl+A gesture");
    require(!state.take_completed().has_value(),
        "A completed capture must not be returned twice");

    require(!state.handle(WM_KEYDOWN, physical(L'B')).suppress,
        "Keyboard input must pass after capture completes");
}

void ignores_auto_repeat_and_duplicate_release() {
    KeyboardCaptureState state;
    state.begin(CaptureMode::legacy);
    const auto action = physical(L'Q', 0x10);

    require_suppressed(state.handle(WM_KEYDOWN, action),
        "Initial action key down must be suppressed");
    const auto repeat = state.handle(WM_KEYDOWN, action);
    require_suppressed(repeat, "Auto-repeat must remain suppressed");
    require(!repeat.completed, "Auto-repeat must not complete the capture");

    const auto first_up = state.handle(WM_KEYUP, action);
    require_suppressed(first_up, "Action key up must be suppressed");
    require(first_up.completed, "The first action key up must complete capture");
    const auto duplicate_up = state.handle(WM_KEYUP, action);
    require(!duplicate_up.suppress,
        "A key-up after completion must pass through");

    const auto completion = state.take_completed();
    require(completion.has_value(), "The repeated-key capture must be retained");
    require(completion->legacy_gesture == simpilot::HotKeyGesture{0, L'Q'},
        "Repeated key-down must not alter the captured action");
    require(!state.take_completed().has_value(),
        "The repeated-key completion must be consumable once");
}

void preserves_left_and_right_modifier_classes() {
    KeyboardCaptureState left;
    left.begin(CaptureMode::legacy);
    const auto left_control = physical(VK_LCONTROL, 0x1D);
    const auto left_down = left.handle(WM_KEYDOWN, left_control);
    require_suppressed(left_down, "Left Ctrl down must be suppressed");
    const auto left_action = physical(L'F', 0x21);
    require_suppressed(left.handle(WM_KEYDOWN, left_action),
        "Left Ctrl action down must be suppressed");
    require_suppressed(left.handle(WM_KEYUP, left_action),
        "Left Ctrl action up must be suppressed before modifier release");
    const auto left_modifier_up = left.handle(WM_KEYUP, left_control);
    require_suppressed(left_modifier_up,
        "Left Ctrl up must be suppressed after the action release");
    require(left_modifier_up.completed,
        "Left Ctrl capture must complete after both keys are released");
    const auto left_completion = left.take_completed();
    require(left_completion.has_value(), "Left Ctrl capture must complete");
    require(left_completion->legacy_gesture == simpilot::HotKeyGesture{MOD_CONTROL, L'F'},
        "Left Ctrl must map to the Ctrl modifier class");

    KeyboardCaptureState right;
    right.begin(CaptureMode::legacy);
    const auto right_control = physical(VK_RCONTROL, 0x11D, true);
    require_suppressed(right.handle(WM_KEYDOWN, right_control),
        "Right Ctrl down must be suppressed");
    const auto right_action = physical(L'G', 0x22);
    require_suppressed(right.handle(WM_KEYDOWN, right_action),
        "Right Ctrl action down must be suppressed");
    require_suppressed(right.handle(WM_KEYUP, right_action),
        "Right Ctrl action up must be suppressed before modifier release");
    const auto right_modifier_up = right.handle(WM_KEYUP, right_control);
    require_suppressed(right_modifier_up,
        "Right Ctrl up must be suppressed after the action release");
    require(right_modifier_up.completed,
        "Right Ctrl capture must complete after both keys are released");
    const auto right_completion = right.take_completed();
    require(right_completion.has_value(), "Right Ctrl capture must complete");
    require(right_completion->legacy_gesture == simpilot::HotKeyGesture{MOD_CONTROL, L'G'},
        "Right Ctrl must map to the Ctrl modifier class");
}

std::optional<CaptureCompletion> capture_with_release_order(
    const bool action_first, const PhysicalKey& modifier) {
    KeyboardCaptureState state;
    state.begin(CaptureMode::legacy);
    const auto action = physical(L'H', 0x23);
    require_suppressed(state.handle(WM_KEYDOWN, modifier),
        "Modifier down must be suppressed");
    require_suppressed(state.handle(WM_KEYDOWN, action),
        "Action down must be suppressed");

    if (action_first) {
        const auto action_up = state.handle(WM_KEYUP, action);
        require_suppressed(action_up,
            "Action-first release must remain suppressed");
        require(!action_up.completed,
            "Action-first release must wait for the modifier release");
        const auto modifier_up = state.handle(WM_KEYUP, modifier);
        require_suppressed(modifier_up,
            "Final modifier release must be suppressed");
        require(modifier_up.completed,
            "Final modifier release must complete the capture");
    } else {
        const auto modifier_up = state.handle(WM_KEYUP, modifier);
        require_suppressed(modifier_up,
            "Modifier-first release must remain suppressed");
        require(!modifier_up.completed,
            "Modifier-first release must wait for the action release");
        const auto action_up = state.handle(WM_KEYUP, action);
        require_suppressed(action_up,
            "Final action release must be suppressed");
        require(action_up.completed,
            "Final action release must complete the capture");
    }
    return state.take_completed();
}

void completes_after_either_release_order() {
    const auto action_first = capture_with_release_order(
        true, physical(VK_LSHIFT, 0x2A));
    require(action_first.has_value(), "Action-first release must produce a result");
    require(action_first->legacy_gesture == simpilot::HotKeyGesture{MOD_SHIFT, L'H'},
        "Action-first release must retain the modifier");

    const auto modifier_first = capture_with_release_order(
        false, physical(VK_RMENU, 0x138, true));
    require(modifier_first.has_value(), "Modifier-first release must produce a result");
    require(modifier_first->legacy_gesture == simpilot::HotKeyGesture{MOD_ALT, L'H'},
        "Modifier-first release must retain the modifier");
}

void handles_escape_and_backspace_boundaries() {
    KeyboardCaptureState bare_escape;
    bare_escape.begin(CaptureMode::legacy);
    const auto cancelled = bare_escape.handle(WM_KEYDOWN, physical(VK_ESCAPE, 0x01));
    require(cancelled.suppress && cancelled.cancelled && !cancelled.completed,
        "Bare Escape must cancel a legacy capture");
    require(!bare_escape.active(), "Cancelled capture must become inactive");
    require(!bare_escape.take_completed().has_value(),
        "Cancellation must not leave a completion");
    require(!bare_escape.handle(WM_KEYUP, physical(VK_ESCAPE, 0x01)).suppress,
        "Escape key-up after cancellation must pass through");

    KeyboardCaptureState modified_escape;
    modified_escape.begin(CaptureMode::legacy);
    const auto control = physical(VK_CONTROL, 0x1D);
    const auto escape = physical(VK_ESCAPE, 0x01);
    require_suppressed(modified_escape.handle(WM_KEYDOWN, control),
        "Ctrl down must be suppressed before Ctrl+Escape");
    const auto escape_down = modified_escape.handle(WM_KEYDOWN, escape);
    require_suppressed(escape_down,
        "Ctrl+Escape must be captured instead of cancelled");
    require(!escape_down.cancelled, "Modified Escape must not cancel capture");
    require_suppressed(modified_escape.handle(WM_KEYUP, escape),
        "Ctrl+Escape key-up must be suppressed");
    const auto control_up = modified_escape.handle(WM_KEYUP, control);
    require_suppressed(control_up, "Final Ctrl release must be suppressed");
    require(control_up.completed, "Ctrl+Escape must complete after release");
    const auto modified_completion = modified_escape.take_completed();
    require(modified_completion.has_value(), "Ctrl+Escape must produce a result");
    require(modified_completion->legacy_gesture
                == simpilot::HotKeyGesture{MOD_CONTROL, VK_ESCAPE},
        "Ctrl+Escape must preserve both the modifier and Escape action");

    KeyboardCaptureState backspace;
    backspace.begin(CaptureMode::legacy);
    capture_legacy_key(backspace, physical(VK_BACK, 0x0E));
    const auto backspace_completion = backspace.take_completed();
    require(backspace_completion.has_value(), "Backspace must be a normal action key");
    require(backspace_completion->legacy_gesture
                == simpilot::HotKeyGesture{0, VK_BACK},
        "Backspace must not be treated as a clear or cancel command");

    KeyboardCaptureState mapping_trigger;
    mapping_trigger.begin(CaptureMode::mapping_trigger);
    capture_legacy_key(mapping_trigger, escape);
    const auto mapping_completion = mapping_trigger.take_completed();
    require(mapping_completion.has_value(),
        "Escape must be capturable in mapping-trigger mode");
    require(mapping_completion->mode == CaptureMode::mapping_trigger
                && mapping_completion->trigger.single_key
                && mapping_completion->trigger.action == escape,
        "Mapping-trigger Escape must be recorded as a single key");
}

void reset_and_begin_clear_pending_state() {
    KeyboardCaptureState state;
    state.begin(CaptureMode::legacy);
    const auto first_session = state.session();
    require(first_session != 0, "The first capture session must be nonzero");
    require_suppressed(state.handle(WM_KEYDOWN, physical(VK_LWIN, 0x5B, true)),
        "Win down must be suppressed before reset");
    state.reset();
    require(!state.active() && state.mode() == CaptureMode::legacy,
        "Reset must stop capture and restore legacy mode");
    require(state.session() == first_session,
        "Reset must not rewind the session generation");
    require(!state.take_completed().has_value(),
        "Reset must discard a pending candidate");
    require(!state.handle(WM_KEYUP, physical(VK_LWIN, 0x5B, true)).suppress,
        "Key-up after reset must pass through");

    state.begin(CaptureMode::mapping_output);
    require(state.session() != first_session
                && state.mode() == CaptureMode::mapping_output,
        "A new begin must advance the session and select its mode");
    capture_legacy_key(state, physical(L'J', 0x24));
    state.reset();
    require(!state.take_completed().has_value(),
        "Reset must discard an unconsumed completion");

    state.begin(CaptureMode::legacy);
    capture_legacy_key(state, physical(L'K', 0x25));
    require(state.take_completed().has_value(),
        "A fresh session must still complete after reset");
    require(!state.take_completed().has_value(),
        "Completion must be consumed exactly once");
}

void captures_physical_modifiers_and_simultaneous_chords() {
    KeyboardCaptureState state;
    state.begin(CaptureMode::mapping_trigger);
    const auto right_control = physical(VK_RCONTROL, 0x11D, true);
    const auto first = physical(L'A', 0x1E);
    const auto second = physical(L'S', 0x1F);
    require_suppressed(state.handle(WM_KEYDOWN, right_control),
        "Mapping capture must suppress the physical modifier");
    require_suppressed(state.handle(WM_KEYDOWN, first),
        "Mapping capture must suppress the first chord action");
    require_suppressed(state.handle(WM_KEYDOWN, second),
        "Mapping capture must suppress the second chord action");
    require_suppressed(state.handle(WM_KEYUP, first),
        "Mapping capture must suppress an early action release");
    require_suppressed(state.handle(WM_KEYUP, right_control),
        "Mapping capture must suppress an early modifier release");
    const auto completed = state.handle(WM_KEYUP, second);
    require_suppressed(completed,
        "Mapping capture must suppress the final chord release");
    require(completed.completed,
        "Mapping capture must complete after every chord key is released");
    const auto result = state.take_completed();
    require(result.has_value()
                && !result->trigger.single_key
                && result->trigger.modifier_count == 1
                && result->trigger.modifiers[0] == right_control
                && result->trigger.action == first
                && result->trigger.chord_action == second,
        "Mapping capture must retain physical modifier and chord identities");

    state.begin(CaptureMode::mapping_trigger);
    require_suppressed(state.handle(WM_KEYDOWN, second),
        "A bare chord may start with either action");
    require_suppressed(state.handle(WM_KEYDOWN, first),
        "A bare chord must accept a second simultaneous action");
    require_suppressed(state.handle(WM_KEYUP, first),
        "Bare chord action release must be suppressed");
    require(state.handle(WM_KEYUP, second).completed,
        "A bare two-action chord must complete without modifiers");
    const auto bare = state.take_completed();
    require(bare.has_value() && bare->trigger.modifier_count == 0
                && bare->trigger.chord_action.has_value(),
        "A captured bare chord must remain a chord");
}

void captures_a_standalone_physical_modifier_source() {
    KeyboardCaptureState state;
    state.begin(CaptureMode::mapping_trigger);
    const auto right_control = physical(VK_RCONTROL, 0x1D, true);
    require_suppressed(state.handle(WM_KEYDOWN, right_control),
        "A standalone Right Ctrl down must be suppressed while recording");
    const auto released = state.handle(WM_KEYUP, right_control);
    require_suppressed(released,
        "A standalone Right Ctrl up must be suppressed while recording");
    require(released.completed && !state.active(),
        "A standalone physical modifier must complete mapping capture");
    const auto completion = state.take_completed();
    require(completion.has_value()
                && completion->trigger.single_key
                && completion->trigger.modifier_count == 0
                && completion->trigger.action == right_control,
        "Right Ctrl must be promoted from a modifier to a single source action");

    state.begin(CaptureMode::mapping_trigger);
    const auto left_shift = physical(VK_LSHIFT, 0x2A, false);
    require_suppressed(state.handle(WM_KEYDOWN, right_control),
        "The first modifier of an incomplete combination must be suppressed");
    require_suppressed(state.handle(WM_KEYDOWN, left_shift),
        "The second modifier of an incomplete combination must be suppressed");
    require_suppressed(state.handle(WM_KEYUP, left_shift),
        "An incomplete modifier combination release must be suppressed");
    const auto final_release = state.handle(WM_KEYUP, right_control);
    require_suppressed(final_release,
        "The final incomplete modifier release must be suppressed");
    require(!final_release.completed && state.active(),
        "Multiple modifiers without an action must not become a single source");
    state.end();

    state.begin(CaptureMode::mapping_output);
    require_suppressed(state.handle(WM_KEYDOWN, right_control),
        "A target modifier down must still be recordable as a prefix");
    const auto output_release = state.handle(WM_KEYUP, right_control);
    require_suppressed(output_release,
        "A target modifier release must remain suppressed while recording");
    require(!output_release.completed && state.active(),
        "A modifier alone must remain invalid as a mapping target");
    state.end();
}

} // namespace

int wmain() {
    try {
        captures_all_keyboard_hook_message_variants();
        ignores_auto_repeat_and_duplicate_release();
        preserves_left_and_right_modifier_classes();
        completes_after_either_release_order();
        handles_escape_and_backspace_boundaries();
        reset_and_begin_clear_pending_state();
        captures_physical_modifiers_and_simultaneous_chords();
        captures_a_standalone_physical_modifier_source();
        std::wcout << L"All keyboard capture state tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
