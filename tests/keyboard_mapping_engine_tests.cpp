#include "keyboard_mapping_engine.hpp"
#include "keyboard_mapping_editor_model.hpp"
#include "simpilot/localization.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using simpilot::KeyboardMappingEngine;
using simpilot::KeyboardMappingDraftError;
using simpilot::KeyboardMappingEditorModel;
using simpilot::KeyboardMappingRule;
using simpilot::KeyboardOutput;
using simpilot::KeyboardTrigger;
using simpilot::MappingEventDecision;
using simpilot::PhysicalKey;

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

PhysicalKey key(const UINT virtual_key, const UINT scan_code) {
    return {.virtual_key = virtual_key, .scan_code = scan_code, .extended = false};
}

bool contains_key(
    const std::vector<PhysicalKey>& keys, const PhysicalKey& expected) {
    return std::ranges::find(keys, expected) != keys.end();
}

KBDLLHOOKSTRUCT event_for(const PhysicalKey physical, const ULONG_PTR extra = 0) {
    return {
        .vkCode = physical.virtual_key,
        .scanCode = physical.scan_code,
        .flags = physical.extended ? DWORD{LLKHF_EXTENDED} : DWORD{0},
        .time = 0,
        .dwExtraInfo = extra,
    };
}

KeyboardMappingRule single_rule(
    const PhysicalKey source, const PhysicalKey target,
    std::wstring process = {}) {
    KeyboardMappingRule rule;
    rule.trigger.single_key = true;
    rule.trigger.action = source;
    rule.output.single_key = true;
    rule.output.action = target;
    rule.process_name = std::move(process);
    return rule;
}

KeyboardMappingRule shortcut_rule(
    const PhysicalKey modifier, const PhysicalKey source,
    const PhysicalKey target, std::wstring process = {}) {
    KeyboardMappingRule rule;
    rule.trigger.modifier_count = 1;
    rule.trigger.modifiers[0] = modifier;
    rule.trigger.action = source;
    rule.output.single_key = true;
    rule.output.action = target;
    rule.process_name = std::move(process);
    return rule;
}

KeyboardMappingRule chord_rule(
    const PhysicalKey first, const PhysicalKey second,
    const PhysicalKey target) {
    KeyboardMappingRule rule;
    rule.trigger.action = first;
    rule.trigger.chord_action = second;
    rule.output.single_key = true;
    rule.output.action = target;
    return rule;
}

void single_key_lifecycle() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    require(engine.replace_rules(true, {single_rule(key(L'A', 30), key(L'B', 48))}),
            "single-key rule must compile");

    auto source_down = event_for(key(L'A', 30));
    auto result = engine.handle(WM_KEYDOWN, source_down);
    require(result.decision == MappingEventDecision::suppress,
            "mapped source key-down must be suppressed");
    require(injected.size() == 1
                && injected[0].ki.wScan == 48
                && (injected[0].ki.dwFlags & KEYEVENTF_SCANCODE) != 0
                && injected[0].ki.dwExtraInfo == KeyboardMappingEngine::target_injected_marker,
            "target key-down must preserve physical output metadata");

    result = engine.handle(WM_KEYUP, source_down);
    require(result.decision == MappingEventDecision::suppress,
            "mapped source key-up must be suppressed");
    require(injected.size() == 2
                && (injected[1].ki.dwFlags & KEYEVENTF_KEYUP) != 0,
            "target key-up must be emitted after source release");

    auto target_event = event_for(key(L'B', 48),
                                  KeyboardMappingEngine::target_injected_marker);
    require(engine.handle(WM_KEYDOWN, target_event).decision
                == MappingEventDecision::pass,
            "target-marked input must bypass mapping");
}

void physical_modifier_single_key_disambiguation() {
    const auto modifiers = simpilot::keyboard_mapping_modifier_catalog();
    const auto right_control = modifiers[1];
    const auto left_shift = modifiers[4];
    const auto left_win = modifiers[6];
    const auto f23 = simpilot::keyboard_mapping_catalog_key(VK_F23);

    KeyboardMappingRule copilot;
    copilot.trigger.single_key = true;
    copilot.trigger.action = right_control;
    copilot.output.modifier_count = 2;
    copilot.output.modifiers[0] = left_shift;
    copilot.output.modifiers[1] = left_win;
    copilot.output.action = f23;

    std::uint64_t now = 0;
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        },
        [&now] { return now; });
    require(engine.replace_rules(true, {copilot}),
            "a physical modifier must be accepted as a single source key");

    const auto control_event = event_for(right_control);
    require(engine.handle(WM_KEYDOWN, control_event).decision
                == MappingEventDecision::suppress
                && engine.pending() && injected.empty(),
            "Right Ctrl down must wait for modifier disambiguation");
    require(engine.handle(WM_KEYDOWN, control_event).decision
                == MappingEventDecision::suppress
                && injected.empty(),
            "Right Ctrl auto-repeat must stay buffered without duplicate events");
    require(engine.handle(WM_KEYUP, control_event).decision
                == MappingEventDecision::suppress
                && !engine.pending() && injected.size() == 6,
            "a short Right Ctrl tap must emit one complete Copilot shortcut");
    require(injected[0].ki.wScan == left_shift.scan_code
                && injected[1].ki.wScan == left_win.scan_code
                && injected[2].ki.wScan == f23.scan_code
                && (injected[3].ki.dwFlags & KEYEVENTF_KEYUP) != 0
                && injected[3].ki.wScan == f23.scan_code
                && (injected[4].ki.dwFlags & KEYEVENTF_KEYUP) != 0
                && injected[4].ki.wScan == left_win.scan_code
                && (injected[5].ki.dwFlags & KEYEVENTF_KEYUP) != 0
                && injected[5].ki.wScan == left_shift.scan_code
                && std::ranges::all_of(injected, [](const INPUT& input) {
                    return input.ki.dwExtraInfo
                        == KeyboardMappingEngine::target_injected_marker;
                }),
            "Copilot target must be Left Shift + Left Win + F23 with reverse release");

    injected.clear();
    require(engine.handle(WM_KEYDOWN, control_event).decision
                == MappingEventDecision::suppress,
            "a second Right Ctrl gesture must enter disambiguation");
    const auto c_event = event_for(key(L'C', 46));
    require(engine.handle(WM_KEYDOWN, c_event).decision
                == MappingEventDecision::pass
                && injected.size() == 1
                && injected[0].ki.wScan == right_control.scan_code
                && (injected[0].ki.dwFlags & KEYEVENTF_KEYUP) == 0
                && injected[0].ki.dwExtraInfo
                    == KeyboardMappingEngine::replay_injected_marker,
            "Ctrl+C must replay Right Ctrl down before passing C through");
    require(engine.handle(WM_KEYUP, c_event).decision
                == MappingEventDecision::pass
                && engine.handle(WM_KEYUP, control_event).decision
                    == MappingEventDecision::pass,
            "ordinary Ctrl+C releases must pass after disambiguation");

    injected.clear();
    require(engine.handle(WM_KEYDOWN, control_event).decision
                == MappingEventDecision::suppress,
            "a held Right Ctrl must enter disambiguation");
    now = KeyboardMappingEngine::pending_timeout_ms;
    engine.on_timer();
    require(!engine.pending() && injected.size() == 3,
            "a held Right Ctrl must activate the target at the deadline");
    require(engine.handle(WM_KEYUP, control_event).decision
                == MappingEventDecision::suppress
                && injected.size() == 6,
            "releasing a committed Right Ctrl mapping must release the target");

    injected.clear();
    now = 0;
    require(engine.handle(WM_KEYDOWN, control_event).decision
                == MappingEventDecision::suppress,
            "a pending modifier must be resettable");
    engine.reset(true);
    require(!engine.pending() && injected.size() == 1
                && injected[0].ki.dwExtraInfo
                    == KeyboardMappingEngine::replay_injected_marker,
            "reset must replay a buffered physical modifier");
    require(engine.handle(WM_KEYUP, control_event).decision
                == MappingEventDecision::pass,
            "the physical modifier release must pass after reset replay");

    injected.clear();
    KeyboardMappingEngine shortcut_engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    const auto shortcut = shortcut_rule(
        right_control, key(L'C', 46), key(L'B', 48));
    require(shortcut_engine.replace_rules(true, {copilot, shortcut}),
            "a bare modifier and its longer shortcut must coexist");
    require(shortcut_engine.handle(WM_KEYDOWN, control_event).decision
                == MappingEventDecision::suppress,
            "the overlapping Right Ctrl prefix must be buffered");
    require(shortcut_engine.handle(WM_KEYDOWN, c_event).decision
                == MappingEventDecision::suppress
                && injected.size() == 1
                && injected[0].ki.wScan == 48
                && injected[0].ki.dwExtraInfo
                    == KeyboardMappingEngine::target_injected_marker,
            "a configured Right Ctrl+C rule must beat the bare modifier candidate");
    require(shortcut_engine.handle(WM_KEYUP, c_event).decision
                == MappingEventDecision::suppress,
            "the longer mapping must retain its target until all sources release");
    require(shortcut_engine.handle(WM_KEYUP, control_event).decision
                == MappingEventDecision::suppress
                && injected.size() == 2
                && (injected[1].ki.dwFlags & KEYEVENTF_KEYUP) != 0,
            "the longer mapping target must release with the final source key");
}

void auto_repeat_repeats_the_target_action() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    require(engine.replace_rules(true,
        {single_rule(key(L'A', 30), key(L'B', 48))}),
        "repeat source rule must compile");
    const auto source = event_for(key(L'A', 30));
    require(engine.handle(WM_KEYDOWN, source).decision
                == MappingEventDecision::suppress,
            "initial mapped key-down must be suppressed");
    require(engine.handle(WM_KEYDOWN, source).decision
                == MappingEventDecision::suppress,
            "source auto-repeat must remain suppressed");
    require(injected.size() == 2
                && injected[0].ki.wScan == 48
                && injected[1].ki.wScan == 48
                && (injected[1].ki.dwFlags & KEYEVENTF_KEYUP) == 0,
            "source auto-repeat must repeat the target action key-down");
    require(engine.handle(WM_KEYUP, source).decision
                == MappingEventDecision::suppress,
            "the repeated source release must remain suppressed");
    require(injected.size() == 3
                && (injected.back().ki.dwFlags & KEYEVENTF_KEYUP) != 0,
            "the repeated target must still receive one final release");
}

void secure_combinations_bypass_single_key_mapping() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    const PhysicalKey left_win{VK_LWIN, 91, true};

    require(engine.replace_rules(true,
        {single_rule(key(L'L', 38), key(L'B', 48))}),
        "single L rule must compile");
    auto win = event_for(left_win);
    auto letter_l = event_for(key(L'L', 38));
    require(engine.handle(WM_KEYDOWN, win).decision == MappingEventDecision::pass,
            "an unrelated Windows key must pass before a single-key mapping");
    require(engine.handle(WM_KEYDOWN, letter_l).decision
                == MappingEventDecision::pass,
            "single-key mapping must not swallow a live Win+L combination");
    require(injected.empty(), "Win+L bypass must not inject a target key");

    engine.reset(false);
    require(engine.replace_rules(true,
        {single_rule(key(L'A', 30), key(L'L', 38))}),
        "single key to L rule must compile");
    auto letter_a = event_for(key(L'A', 30));
    require(engine.handle(WM_KEYDOWN, win).decision == MappingEventDecision::pass,
            "Windows key must pass before target safety check");
    require(engine.handle(WM_KEYDOWN, letter_a).decision
                == MappingEventDecision::pass,
            "mapping must not generate L while a forwarded Windows key is down");
    require(injected.empty(), "implicit Win+L target must not be injected");

    engine.reset(false);
    auto secure_target = single_rule(key(L'A', 30), key(VK_DELETE, 83));
    secure_target.output.single_key = false;
    secure_target.output.modifier_count = 1;
    secure_target.output.modifiers[0] = key(VK_LMENU, 56);
    require(engine.replace_rules(true, {secure_target}),
            "Alt+Delete target rule must compile without a live Control key");
    auto control = event_for(key(VK_LCONTROL, 29));
    require(engine.handle(WM_KEYDOWN, control).decision
                == MappingEventDecision::pass,
            "Control key must pass before target safety check");
    require(engine.handle(WM_KEYDOWN, letter_a).decision
                == MappingEventDecision::pass,
            "mapping must not combine live Control with target Alt+Delete");
    require(injected.empty(),
            "implicit secure attention target must not be injected");
}

void prefix_timeout_replays_original_events() {
    std::vector<INPUT> injected;
    std::uint64_t now = 100;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        },
        [&now] { return now; });
    require(engine.replace_rules(true,
        {shortcut_rule(key(VK_LCONTROL, 29), key(L'A', 30), key(L'B', 48))}),
        "shortcut rule must compile");

    auto ctrl = event_for(key(VK_LCONTROL, 29));
    require(engine.handle(WM_KEYDOWN, ctrl).decision
                == MappingEventDecision::suppress,
            "modifier prefix must be held while waiting");
    require(engine.pending() && injected.empty(),
            "prefix should be buffered before timeout");

    now = 349;
    engine.on_timer();
    require(engine.pending() && injected.empty(),
            "prefix must not replay before 250 ms");
    now = 350;
    engine.on_timer();
    require(!engine.pending() && injected.size() == 1
                && injected[0].ki.dwExtraInfo == KeyboardMappingEngine::replay_injected_marker
                && (injected[0].ki.dwFlags & KEYEVENTF_SCANCODE) != 0
                && injected[0].ki.wScan == 29,
            "timed-out prefix must replay with replay marker");

    require(engine.handle(WM_KEYUP, ctrl).decision == MappingEventDecision::pass,
            "released modifier after replay must pass normally");
}

void prefix_mismatch_replays_prefix_and_passes_current_key() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    require(engine.replace_rules(true,
        {shortcut_rule(key(VK_LCONTROL, 29), key(L'A', 30), key(L'B', 48))}),
        "shortcut rule must compile");

    const auto ctrl = event_for(key(VK_LCONTROL, 29));
    const auto unrelated = event_for(key(L'Q', 16));
    require(engine.handle(WM_KEYDOWN, ctrl).decision
                == MappingEventDecision::suppress,
            "modifier prefix must be buffered");
    require(engine.handle(WM_KEYDOWN, unrelated).decision
                == MappingEventDecision::pass,
            "mismatching key-down must pass after prefix replay");
    require(injected.size() == 1
                && injected.front().ki.wScan == 29
                && (injected.front().ki.dwFlags & KEYEVENTF_SCANCODE) != 0
                && injected.front().ki.dwExtraInfo
                    == KeyboardMappingEngine::replay_injected_marker,
            "mismatching key-down must replay the buffered prefix");
}

void accepts_extended_scan_code_values() {
    require(simpilot::is_mapping_key_valid(key(VK_LSHIFT, 0x2A)),
            "ordinary scan codes must remain valid");
    require(simpilot::is_mapping_key_valid(key(VK_RCONTROL, 0x11D)),
            "extended scan codes above 0xFF must be valid");
    require(simpilot::is_mapping_key_valid(key(VK_F13, 0xFFFF)),
            "the maximum persisted scan code must be valid");
    require(!simpilot::is_mapping_key_valid(key(VK_F13, 0x10000)),
            "scan codes beyond WORD range must be rejected");
}

void normalizes_extended_scan_codes_for_injection_and_replay() {
    const auto right_control = PhysicalKey{VK_RCONTROL, 0x11D, true};
    const auto extended_action = PhysicalKey{VK_F13, 0x11D, true};
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    KeyboardMappingRule direct;
    direct.trigger.single_key = true;
    direct.trigger.action = key(L'A', 30);
    direct.output.single_key = true;
    direct.output.action = extended_action;
    require(engine.replace_rules(true, {direct}),
            "extended target mapping must compile");
    const auto source = event_for(key(L'A', 30));
    require(engine.handle(WM_KEYDOWN, source).decision
                == MappingEventDecision::suppress,
            "extended target source must be suppressed");
    require(injected.size() == 1
                && injected.front().ki.wScan == 0x1D
                && (injected.front().ki.dwFlags & KEYEVENTF_SCANCODE) != 0
                && (injected.front().ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0,
            "extended target scan code must be normalized for SendInput");

    std::uint64_t now = 0;
    injected.clear();
    KeyboardMappingEngine replay_engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        },
        [&now] { return now; });
    auto shortcut = shortcut_rule(right_control, key(L'A', 30), key(L'B', 48));
    require(replay_engine.replace_rules(true, {shortcut}),
            "extended modifier mapping must compile");
    const auto modifier = event_for(right_control);
    require(replay_engine.handle(WM_KEYDOWN, modifier).decision
                == MappingEventDecision::suppress,
            "extended modifier prefix must be buffered");
    now = KeyboardMappingEngine::pending_timeout_ms;
    replay_engine.on_timer();
    require(injected.size() == 1
                && injected.front().ki.wScan == 0x1D
                && (injected.front().ki.dwFlags & KEYEVENTF_SCANCODE) != 0
                && (injected.front().ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0
                && injected.front().ki.dwExtraInfo
                    == KeyboardMappingEngine::replay_injected_marker,
            "extended prefix replay must preserve the extended physical key");
}

void action_first_does_not_start_shortcut_mapping() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    require(engine.replace_rules(true,
        {shortcut_rule(key(VK_LCONTROL, 29), key(L'A', 30), key(L'B', 48))}),
        "shortcut rule must compile");

    const auto action = event_for(key(L'A', 30));
    const auto modifier = event_for(key(VK_LCONTROL, 29));
    require(engine.handle(WM_KEYDOWN, action).decision
                == MappingEventDecision::pass,
            "an action pressed before its modifier must pass through");
    require(engine.handle(WM_KEYDOWN, modifier).decision
                == MappingEventDecision::pass,
            "a later modifier must not retroactively activate the shortcut");
    require(injected.empty(),
            "action-first input must not inject a mapped target");
    require(engine.handle(WM_KEYUP, action).decision
                == MappingEventDecision::pass,
            "action release must remain visible after pass-through");
    require(engine.handle(WM_KEYUP, modifier).decision
                == MappingEventDecision::pass,
            "modifier release must remain visible after pass-through");
}

void shortcut_activation_waits_for_all_source_releases() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    require(engine.replace_rules(true,
        {shortcut_rule(key(VK_LCONTROL, 29), key(L'A', 30), key(L'B', 48))}),
        "shortcut rule must compile");
    auto ctrl = event_for(key(VK_LCONTROL, 29));
    auto action = event_for(key(L'A', 30));

    (void)engine.handle(WM_KEYDOWN, ctrl);
    require(engine.handle(WM_KEYDOWN, action).decision
                == MappingEventDecision::suppress,
            "complete shortcut must be suppressed");
    require(injected.size() == 1 && injected[0].ki.wScan == 48,
            "shortcut activation must press target once");

    require(engine.handle(WM_KEYUP, ctrl).decision
                == MappingEventDecision::suppress,
            "modifier release must remain suppressed while action is down");
    require(injected.size() == 1,
            "target must remain held until the final source release");
    require(engine.handle(WM_KEYUP, action).decision
                == MappingEventDecision::suppress,
            "final source release must be suppressed");
    require(injected.size() == 2
                && (injected.back().ki.dwFlags & KEYEVENTF_KEYUP) != 0,
            "target must release after the final source release");
}

void chord_accepts_either_action_order() {
    const auto first = key(L'A', 30);
    const auto second = key(L'S', 31);
    const auto target = key(L'D', 32);
    require(simpilot::validate_keyboard_mappings(
                {chord_rule(first, second, target)}).empty(),
            "a chord without modifiers must be valid");

    for (const auto order : {
             std::array<PhysicalKey, 2>{first, second},
             std::array<PhysicalKey, 2>{second, first},
         }) {
        std::vector<INPUT> injected;
        KeyboardMappingEngine engine(
            [&injected](const INPUT* inputs, const UINT count) {
                injected.insert(injected.end(), inputs, inputs + count);
                return count;
            });
        require(engine.replace_rules(true, {chord_rule(first, second, target)}),
                "a two-action chord must compile");
        require(engine.handle(WM_KEYDOWN, event_for(order[0])).decision
                    == MappingEventDecision::suppress,
                "the first chord action must be buffered");
        require(engine.handle(WM_KEYDOWN, event_for(order[1])).decision
                    == MappingEventDecision::suppress,
                "the completed chord must be suppressed");
        require(injected.size() == 1 && injected.front().ki.wScan == 32,
                "either chord order must press the same target");
        require(engine.handle(WM_KEYUP, event_for(order[0])).decision
                    == MappingEventDecision::suppress,
                "the first chord release must remain suppressed");
        require(engine.handle(WM_KEYUP, event_for(order[1])).decision
                    == MappingEventDecision::suppress,
                "the final chord release must be suppressed");
        require(injected.size() == 2
                    && (injected.back().ki.dwFlags & KEYEVENTF_KEYUP) != 0,
                "the chord target must release after both actions");
    }

    auto reversed = chord_rule(second, first, target);
    require(!simpilot::validate_keyboard_mappings(
                 {chord_rule(first, second, target), reversed}).empty(),
            "reversing simultaneous chord actions must not avoid duplicate detection");
}

void physical_source_identity_is_strict() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    require(engine.replace_rules(true,
        {single_rule(key(L'A', 30), key(L'B', 48))}),
        "physical source rule must compile");

    const auto other_physical_a = event_for(key(L'A', 31));
    require(engine.handle(WM_KEYDOWN, other_physical_a).decision
                == MappingEventDecision::pass,
            "a different scan code with the same virtual key must not match");
    require(engine.handle(WM_KEYUP, other_physical_a).decision
                == MappingEventDecision::pass,
            "the unmatched physical key release must pass");
    require(injected.empty(),
            "a different physical source must not inject a target");
}

void runtime_update_releases_active_output() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    require(engine.replace_rules(true,
        {single_rule(key(L'A', 30), key(L'B', 48))}),
        "initial runtime rule must compile");
    require(engine.handle(WM_KEYDOWN, event_for(key(L'A', 30))).decision
                == MappingEventDecision::suppress,
            "the initial source must activate its target");
    require(engine.replace_rules(true,
        {single_rule(key(L'Q', 16), key(L'R', 19))}),
        "runtime rule replacement must succeed");
    require(injected.size() == 2
                && injected.front().ki.wScan == 48
                && injected.back().ki.wScan == 48
                && (injected.back().ki.dwFlags & KEYEVENTF_KEYUP) != 0,
            "runtime replacement must release every active old target");
}

void process_specific_rule_wins_over_global_rule() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    auto global = single_rule(key(L'A', 30), key(L'B', 48));
    auto scoped = single_rule(key(L'A', 30), key(L'C', 46), L"Editor.EXE");
    require(engine.replace_rules(true, {global, scoped}),
            "global and scoped rules must compile");
    engine.set_foreground_process(L"editor.exe");
    auto source = event_for(key(L'A', 30));
    require(engine.handle(WM_KEYDOWN, source).decision
                == MappingEventDecision::suppress,
            "scoped source must be suppressed");
    require(injected.size() == 1 && injected[0].ki.wScan == 46,
            "process-specific rule must beat global rule");
}

void longest_non_exact_process_scope_wins() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    auto broad = single_rule(key(L'A', 30), key(L'B', 48), L"app");
    broad.exact_match = false;
    auto specific = single_rule(
        key(L'A', 30), key(L'C', 46), L"application");
    specific.exact_match = false;
    require(engine.replace_rules(true, {broad, specific}),
            "overlapping non-exact process scopes must compile");
    engine.set_foreground_process(L"application.exe");

    const auto source = event_for(key(L'A', 30));
    require(engine.handle(WM_KEYDOWN, source).decision
                == MappingEventDecision::suppress,
            "a matching scoped source must be suppressed");
    require(injected.size() == 1 && injected.front().ki.wScan == 46,
            "the longest matching non-exact process scope must win");
}

void exact_process_scope_beats_broad_scope_in_any_order() {
    auto broad = single_rule(key(L'A', 30), key(L'B', 48), L"app");
    broad.exact_match = false;
    auto exact = single_rule(
        key(L'A', 30), key(L'C', 46), L"application.exe");
    exact.exact_match = true;

    for (const auto rules : {
             std::vector<KeyboardMappingRule>{broad, exact},
             std::vector<KeyboardMappingRule>{exact, broad},
         }) {
        std::vector<INPUT> injected;
        KeyboardMappingEngine engine(
            [&injected](const INPUT* inputs, const UINT count) {
                injected.insert(injected.end(), inputs, inputs + count);
                return count;
            });
        require(engine.replace_rules(true, rules),
                "exact and broad process scopes must compile");
        engine.set_foreground_process(L"application.exe");
        require(engine.handle(WM_KEYDOWN, event_for(key(L'A', 30))).decision
                    == MappingEventDecision::suppress,
                "a matching scoped source must be suppressed");
        require(injected.size() == 1 && injected.front().ki.wScan == 46,
                "exact process scope must win regardless of rule order");
    }
}

void rejects_reachable_modifier_subset_prefixes() {
    auto short_rule = shortcut_rule(
        key(VK_LCONTROL, 29), key(L'A', 30), key(L'B', 48));
    auto long_rule = short_rule;
    long_rule.trigger.modifier_count = 2;
    long_rule.trigger.modifiers[1] = key(VK_LSHIFT, 42);
    long_rule.output.action = key(L'C', 46);
    require(!simpilot::validate_keyboard_mappings({short_rule, long_rule}).empty(),
            "a modifier-subset shortcut prefix must be rejected");

    auto chord = short_rule;
    chord.trigger.chord_action = key(L'D', 32);
    require(!simpilot::validate_keyboard_mappings({short_rule, chord}).empty(),
            "a shortcut and its reachable chord extension must be rejected");
}

void reprocesses_current_key_after_prefix_mismatch() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    require(engine.replace_rules(true, {
        shortcut_rule(key(VK_LCONTROL, 29), key(L'A', 30), key(L'B', 48)),
        single_rule(key(L'Q', 16), key(L'R', 19)),
    }), "prefix and independent mappings must compile");

    const auto ctrl = event_for(key(VK_LCONTROL, 29));
    const auto q = event_for(key(L'Q', 16));
    require(engine.handle(WM_KEYDOWN, ctrl).decision
                == MappingEventDecision::suppress,
            "the first shortcut key must be buffered");
    require(engine.handle(WM_KEYDOWN, q).decision
                == MappingEventDecision::suppress,
            "a mismatching key that has its own mapping must be suppressed");
    require(injected.size() == 2
                && injected[0].ki.dwExtraInfo
                    == KeyboardMappingEngine::replay_injected_marker
                && injected[0].ki.wScan == 29
                && injected[1].ki.dwExtraInfo
                    == KeyboardMappingEngine::target_injected_marker
                && injected[1].ki.wScan == 19,
            "prefix replay must precede reprocessing of the current mapping");

    require(engine.handle(WM_KEYUP, q).decision
                == MappingEventDecision::suppress,
            "the remapped current key release must be suppressed");
    require(injected.size() == 3
                && (injected.back().ki.dwFlags & KEYEVENTF_KEYUP) != 0,
            "the remapped current key must release its target");
    require(engine.handle(WM_KEYUP, ctrl).decision
                == MappingEventDecision::pass,
            "the replayed prefix release must pass normally");
}

void replay_marker_does_not_recurse() {
    KeyboardMappingEngine engine(
        [](const INPUT*, const UINT count) { return count; });
    require(engine.replace_rules(true,
        {single_rule(key(L'A', 30), key(L'B', 48))}),
            "single-key rule must compile");
    auto replay = event_for(key(L'A', 30),
                            KeyboardMappingEngine::replay_injected_marker);
    require(engine.handle(WM_KEYDOWN, replay).decision
                == MappingEventDecision::pass,
            "replayed input must bypass mapping");
    require(!engine.pending(), "replayed input must not create a prefix");
}

void arbitrary_injected_input_bypasses_mapping() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    require(engine.replace_rules(true,
        {single_rule(key(L'A', 30), key(L'B', 48))}),
        "single-key rule must compile");

    const auto synthetic = event_for(
        key(L'A', 30), static_cast<ULONG_PTR>(0x1234));
    auto injected_event = synthetic;
    injected_event.flags |= LLKHF_INJECTED;
    require(engine.handle(WM_KEYDOWN, injected_event).decision
                == MappingEventDecision::pass,
            "arbitrary injected input must bypass mapping");
    require(injected.empty() && !engine.pending(),
            "bypassed injected input must not emit or buffer events");
}

void failed_output_injection_is_reported_and_cleaned() {
    std::vector<UINT> batch_sizes;
    KeyboardMappingEngine engine(
        [&batch_sizes](const INPUT*, const UINT count) {
            batch_sizes.push_back(count);
            return UINT{0};
        });
    require(engine.replace_rules(true,
        {single_rule(key(L'A', 30), key(L'B', 48))}),
            "single-key rule must compile");
    auto source = event_for(key(L'A', 30));
    const auto result = engine.handle(WM_KEYDOWN, source);
    require(result.decision == MappingEventDecision::pass,
            "failed injection must not permanently swallow source input");
    require(engine.consume_diagnostic(),
            "failed injection must expose a diagnostic");
    require(batch_sizes.size() >= 2,
            "failed activation must attempt a best-effort key-up cleanup");
}

void editor_model_saves_recorded_shortcuts_without_text_parsing() {
    const auto modifiers = simpilot::keyboard_mapping_modifier_catalog();
    const auto f23 = simpilot::keyboard_mapping_catalog_key(VK_F23);
    const auto f24 = simpilot::keyboard_mapping_catalog_key(VK_F24);

    KeyboardMappingRule rule;
    rule.trigger.modifier_count = 2;
    rule.trigger.modifiers[0] = modifiers[4]; // Left Shift.
    rule.trigger.modifiers[1] = modifiers[6]; // Left Win.
    rule.trigger.action = f23;
    rule.output.modifier_count = 1;
    rule.output.modifiers[0] = modifiers[1]; // Right Ctrl.
    rule.output.action = f24;

    const KeyboardMappingEditorModel editor(rule);
    const auto result = editor.build();
    require(static_cast<bool>(result),
            "recorded shortcuts must save without a text round trip");
    require(result.trigger.modifier_count == 2
                && result.trigger.modifiers[0] == modifiers[4]
                && result.trigger.modifiers[1] == modifiers[6]
                && result.trigger.action == f23,
            "the editor must preserve Left Shift + Left Win + F23");
    require(result.output.modifier_count == 1
                && result.output.modifiers[0] == modifiers[1]
                && result.output.action == f24,
            "the editor must preserve a recorded shortcut target");
}

void editor_model_validates_structured_chords_and_modifiers() {
    const auto modifiers = simpilot::keyboard_mapping_modifier_catalog();
    const auto f23 = simpilot::keyboard_mapping_catalog_key(VK_F23);
    const auto f24 = simpilot::keyboard_mapping_catalog_key(VK_F24);
    const auto action = simpilot::keyboard_mapping_catalog_key(L'A');

    KeyboardMappingEditorModel editor;
    editor.set_source_modifier(0, modifiers[6]); // Deliberately before Shift.
    editor.set_source_modifier(1, modifiers[4]);
    editor.set_source_action(f23);
    editor.set_source_chord_action(action);
    editor.set_target_action(f24);
    auto result = editor.build();
    require(static_cast<bool>(result)
                && result.trigger.modifier_count == 2
                && result.trigger.modifiers[0] == modifiers[4]
                && result.trigger.modifiers[1] == modifiers[6]
                && result.trigger.chord_action == action,
            "structured chord modifiers must be canonicalized");

    editor.set_source_modifier(2, modifiers[4]);
    require(editor.build().error
                == KeyboardMappingDraftError::source_modifier_duplicate,
            "duplicate physical source modifiers must be rejected");
    editor.set_source_modifier(2, modifiers[0]);
    editor.set_source_modifier(3, modifiers[2]);
    require(editor.build().error
                == KeyboardMappingDraftError::source_chord_modifier_limit,
            "a chord must reject a fourth source modifier");

    editor.set_source_modifier(3, std::nullopt);
    editor.set_source_chord_action(f23);
    require(editor.build().error
                == KeyboardMappingDraftError::source_chord_invalid,
            "a chord action must differ from its primary action");
}

void editor_model_supports_a_physical_modifier_source() {
    const auto modifiers = simpilot::keyboard_mapping_modifier_catalog();
    const auto right_control = modifiers[1];
    const auto left_shift = modifiers[4];
    const auto left_win = modifiers[6];
    const auto f23 = simpilot::keyboard_mapping_catalog_key(VK_F23);

    KeyboardMappingEditorModel editor;
    editor.set_source_action(right_control);
    editor.set_target_modifier(0, left_shift);
    editor.set_target_modifier(1, left_win);
    editor.set_target_action(f23);
    auto result = editor.build();
    require(static_cast<bool>(result)
                && result.trigger.single_key
                && result.trigger.action == right_control
                && result.output.modifier_count == 2
                && result.output.action == f23,
            "Right Ctrl must build as a single physical source for Copilot");
    KeyboardMappingRule rule;
    rule.trigger = result.trigger;
    rule.output = result.output;
    require(simpilot::validate_keyboard_mappings({rule}).empty(),
            "the complete Right Ctrl to Copilot rule must validate");

    editor.set_source_modifier(0, left_shift);
    require(editor.build().error
                == KeyboardMappingDraftError::source_modifier_action_requires_single,
            "a modifier source action cannot also have source modifiers");
    editor.set_source_modifier(0, std::nullopt);
    editor.set_source_chord_action(simpilot::keyboard_mapping_catalog_key(L'A'));
    require(editor.build().error
                == KeyboardMappingDraftError::source_modifier_action_requires_single,
            "a modifier source action cannot also have a chord action");
    editor.set_source_chord_action(std::nullopt);
    editor.set_source_action(PhysicalKey{VK_CONTROL, 0x1D, false});
    require(editor.build().error == KeyboardMappingDraftError::source_action_invalid,
            "a generic Ctrl identity must not replace a sided physical source");
}

void editor_catalog_uses_hook_compatible_physical_keys() {
    const auto modifiers = simpilot::keyboard_mapping_modifier_catalog();
    require(modifiers[4] == PhysicalKey{VK_LSHIFT, 0x2A, false},
            "Left Shift must use the low-level hook identity");
    require(modifiers[1] == PhysicalKey{VK_RCONTROL, 0x1D, true},
            "Right Ctrl must split the extended flag from its scan code");
    require(modifiers[6] == PhysicalKey{VK_LWIN, 0x5B, true},
            "Left Win must use an extended low-byte scan code");

    const auto actions = simpilot::keyboard_mapping_action_catalog();
    std::set<std::wstring> action_labels;
    for (auto virtual_key = static_cast<UINT>(VK_F1);
         virtual_key <= static_cast<UINT>(VK_F24); ++virtual_key) {
        require(contains_key(actions,
                    simpilot::keyboard_mapping_catalog_key(virtual_key)),
                "the action catalog must contain every key from F1 through F24");
    }
    require(std::ranges::none_of(actions, [](const PhysicalKey& candidate) {
                return simpilot::is_mapping_modifier(candidate.virtual_key);
            }),
            "the action catalog must not expose modifier keys as actions");
    for (const auto& action : actions) {
        require(action_labels.insert(simpilot::format_mapping_key(action)).second,
                "every catalog action must have a distinct visible label");
    }
    require(contains_key(actions, PhysicalKey{VK_RETURN, 0x1C, false})
                && contains_key(actions, PhysicalKey{VK_RETURN, 0x1C, true}),
            "main Enter and numpad Enter must remain distinct");

    const auto source_actions = simpilot::keyboard_mapping_source_action_catalog();
    require(contains_key(source_actions, modifiers[1])
                && contains_key(source_actions, modifiers[7]),
            "the source primary-key catalog must expose sided modifiers");
    require(simpilot::format_mapping_key(
                simpilot::keyboard_mapping_catalog_key(VK_F23)) == L"F23",
            "VK_F23 must be displayed as F23 instead of decimal VK134");
    require(simpilot::format_mapping_key(
                simpilot::keyboard_mapping_catalog_key(VK_BROWSER_BACK))
                == L"Browser Back",
            "browser keys must not fall back to raw VK values");
    require(simpilot::format_mapping_key(
                simpilot::keyboard_mapping_catalog_key(VK_VOLUME_MUTE))
                == L"Volume Mute",
            "media scan-code collisions must not be displayed as letters");
    require(simpilot::format_mapping_key(
                PhysicalKey{VK_RETURN, 0x1C, false})
                != simpilot::format_mapping_key(
                    PhysicalKey{VK_RETURN, 0x1C, true}),
            "main Enter and numpad Enter labels must differ");
    require(simpilot::format_mapping_key(
                simpilot::keyboard_mapping_catalog_key(VK_OEM_5))
                != simpilot::format_mapping_key(
                    simpilot::keyboard_mapping_catalog_key(VK_OEM_102)),
            "distinct OEM keys must have disambiguated labels");

    constexpr std::array languages{
        simpilot::UiLanguage::english,
        simpilot::UiLanguage::simplified_chinese,
        simpilot::UiLanguage::traditional_chinese,
    };
    const auto language_directory = std::filesystem::path(__FILE__)
        .parent_path().parent_path() / L"Languages";
    for (const auto language : languages) {
        const simpilot::Localization localization(language, language_directory);
        std::set<std::wstring> localized_labels;
        for (const auto& action : source_actions) {
            const auto label = simpilot::localized_keyboard_mapping_key_label(
                action, localization);
            const auto inserted = localized_labels.insert(label).second;
            if (!inserted) {
                std::wcerr << L"Duplicate key label in "
                           << std::wstring(localization.language_code().begin(),
                                           localization.language_code().end())
                           << L": " << label << L'\n';
            }
            require(inserted,
                    "every source-key option must have a distinct localized label");
        }
        require(simpilot::localized_keyboard_mapping_key_label(
                    simpilot::keyboard_mapping_catalog_key(VK_F23),
                    localization) == L"F23",
                "F23 must have the documented label in every language");
    }
}

void editor_model_preserves_unlisted_recorded_keys() {
    KeyboardMappingRule rule;
    rule.trigger.single_key = true;
    rule.trigger.action = PhysicalKey{VK_F13, 0x777, false};
    rule.output.single_key = true;
    rule.output.action = PhysicalKey{VK_F14, 0x778, true};

    const auto result = KeyboardMappingEditorModel(rule).build();
    require(static_cast<bool>(result)
                && result.trigger.action == rule.trigger.action
                && result.output.action == rule.output.action,
            "editing must preserve physical keys outside the standard catalog");
}

} // namespace

int wmain() {
    try {
        single_key_lifecycle();
        physical_modifier_single_key_disambiguation();
        auto_repeat_repeats_the_target_action();
        secure_combinations_bypass_single_key_mapping();
        prefix_timeout_replays_original_events();
        prefix_mismatch_replays_prefix_and_passes_current_key();
        accepts_extended_scan_code_values();
        normalizes_extended_scan_codes_for_injection_and_replay();
        action_first_does_not_start_shortcut_mapping();
        shortcut_activation_waits_for_all_source_releases();
        chord_accepts_either_action_order();
        physical_source_identity_is_strict();
        runtime_update_releases_active_output();
        process_specific_rule_wins_over_global_rule();
        longest_non_exact_process_scope_wins();
        exact_process_scope_beats_broad_scope_in_any_order();
        rejects_reachable_modifier_subset_prefixes();
        reprocesses_current_key_after_prefix_mismatch();
        replay_marker_does_not_recurse();
        arbitrary_injected_input_bypasses_mapping();
        failed_output_injection_is_reported_and_cleaned();
        editor_model_saves_recorded_shortcuts_without_text_parsing();
        editor_model_validates_structured_chords_and_modifiers();
        editor_model_supports_a_physical_modifier_source();
        editor_catalog_uses_hook_compatible_physical_keys();
        editor_model_preserves_unlisted_recorded_keys();
        std::wcout << L"All keyboard mapping engine tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
