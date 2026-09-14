#include "keyboard_mapping_engine.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>

namespace simpilot {
namespace {

constexpr std::size_t no_index = std::numeric_limits<std::size_t>::max();

UINT default_send_input(const INPUT* inputs, const UINT count) noexcept {
    return SendInput(count, const_cast<INPUT*>(inputs), sizeof(INPUT));
}

std::uint64_t default_clock() noexcept {
    return GetTickCount64();
}

void fill_keyboard_input(
    INPUT& input, const PhysicalKey& key, const bool key_down,
    const ULONG_PTR marker) noexcept {
    input = {};
    input.type = INPUT_KEYBOARD;

    // Persisted extended scan codes may use the 0x100 + make-code form.  The
    // Windows input structure carries the extended bit separately, so strip
    // that synthetic prefix before injecting the event.
    auto scan_code = static_cast<WORD>(key.scan_code);
    auto extended = key.extended;
    if ((scan_code & 0xFF00U) == 0x0100U) {
        scan_code = static_cast<WORD>(scan_code & 0x00FFU);
        extended = true;
    }
    input.ki.wVk = scan_code == 0
        ? static_cast<WORD>(key.virtual_key) : 0;
    input.ki.wScan = scan_code;
    input.ki.dwFlags = scan_code != 0 ? KEYEVENTF_SCANCODE : 0;
    if (!key_down) input.ki.dwFlags |= KEYEVENTF_KEYUP;
    if (extended) input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    input.ki.dwExtraInfo = marker;
}

std::size_t trigger_key_count(const KeyboardTrigger& trigger) noexcept {
    return (trigger.single_key ? 0 : trigger.modifier_count)
        + (trigger.action.empty() ? 0 : 1)
        + (trigger.chord_action ? 1 : 0);
}

std::size_t process_scope_length(const std::wstring& process_name) noexcept {
    if (process_name.empty()) return 0;
    constexpr std::wstring_view executable_suffix = L".exe";
    return process_name.ends_with(executable_suffix)
        ? process_name.size() - executable_suffix.size()
        : process_name.size();
}

int compare_physical_key(
    const PhysicalKey& left, const PhysicalKey& right) noexcept {
    if (left.virtual_key != right.virtual_key) {
        return left.virtual_key < right.virtual_key ? -1 : 1;
    }
    if (left.scan_code != right.scan_code) {
        return left.scan_code < right.scan_code ? -1 : 1;
    }
    if (left.extended != right.extended) return left.extended ? 1 : -1;
    return 0;
}

template <typename Candidate>
int compare_modifiers(
    const Candidate& left, const Candidate& right) noexcept {
    const auto left_count = std::min(left.modifier_count, left.modifiers.size());
    const auto right_count = std::min(right.modifier_count, right.modifiers.size());
    if (left_count != right_count) return left_count < right_count ? -1 : 1;

    std::array<PhysicalKey, 4> left_modifiers{};
    std::array<PhysicalKey, 4> right_modifiers{};
    std::ranges::copy_n(left.modifiers.begin(), left_count, left_modifiers.begin());
    std::ranges::copy_n(right.modifiers.begin(), right_count, right_modifiers.begin());
    std::ranges::sort(left_modifiers.begin(), left_modifiers.begin()
        + static_cast<std::ptrdiff_t>(left_count),
        [](const PhysicalKey& a, const PhysicalKey& b) {
            return compare_physical_key(a, b) < 0;
        });
    std::ranges::sort(right_modifiers.begin(), right_modifiers.begin()
        + static_cast<std::ptrdiff_t>(right_count),
        [](const PhysicalKey& a, const PhysicalKey& b) {
            return compare_physical_key(a, b) < 0;
        });
    for (std::size_t index = 0; index < left_count; ++index) {
        if (const auto comparison = compare_physical_key(
                left_modifiers[index], right_modifiers[index]);
            comparison != 0) {
            return comparison;
        }
    }
    return 0;
}

int compare_trigger_identity(
    const KeyboardTrigger& left, const KeyboardTrigger& right) noexcept {
    if (left.single_key != right.single_key) return left.single_key ? -1 : 1;
    if (const auto comparison = compare_modifiers(left, right);
        comparison != 0) {
        return comparison;
    }
    if (const auto comparison = compare_physical_key(left.action, right.action);
        comparison != 0) {
        return comparison;
    }
    if (left.chord_action.has_value() != right.chord_action.has_value()) {
        return left.chord_action ? 1 : -1;
    }
    if (left.chord_action) {
        return compare_physical_key(*left.chord_action, *right.chord_action);
    }
    return 0;
}

int compare_output_identity(
    const KeyboardOutput& left, const KeyboardOutput& right) noexcept {
    if (left.single_key != right.single_key) return left.single_key ? -1 : 1;
    if (const auto comparison = compare_modifiers(left, right);
        comparison != 0) {
        return comparison;
    }
    return compare_physical_key(left.action, right.action);
}

bool expected_trigger_key(
    const KeyboardTrigger& trigger, const PhysicalKey& key) noexcept {
    if (!trigger.single_key) {
        for (std::size_t index = 0; index < trigger.modifier_count; ++index) {
            if (trigger.modifiers[index] == key) return true;
        }
    }
    if (trigger.action == key) return true;
    return trigger.chord_action && *trigger.chord_action == key;
}

} // namespace

KeyboardMappingEngine::KeyboardMappingEngine()
    : KeyboardMappingEngine(
          InputSink{default_send_input}, Clock{default_clock}) {}

KeyboardMappingEngine::KeyboardMappingEngine(
    InputSink input_sink, Clock clock)
    : input_sink_(std::move(input_sink)), clock_(std::move(clock)) {
    if (!input_sink_) input_sink_ = default_send_input;
    if (!clock_) clock_ = default_clock;
}

bool KeyboardMappingEngine::replace_rules(
    const bool enabled, const std::vector<KeyboardMappingRule>& rules) noexcept {
    try {
        const auto errors = validate_keyboard_mappings(rules);
        if (!errors.empty()) {
            mark_diagnostic();
            return false;
        }

        std::vector<CompiledRule> compiled;
        compiled.reserve(rules.size());
        for (const auto& rule : rules) {
            if (!rule.enabled) continue;
            CompiledRule item;
            item.rule = rule;
            item.process_name = normalize_mapping_process_name(rule.process_name);
            if (!rule.process_name.empty() && item.process_name.empty()) {
                mark_diagnostic();
                return false;
            }
            item.rule.process_name = item.process_name;
            compiled.push_back(std::move(item));
        }

        reset(true);
        rules_.swap(compiled);
        enabled_ = enabled;
        return true;
    } catch (...) {
        mark_diagnostic();
        return false;
    }
}

bool KeyboardMappingEngine::is_key_down(const UINT message) noexcept {
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

bool KeyboardMappingEngine::is_key_up(const UINT message) noexcept {
    return message == WM_KEYUP || message == WM_SYSKEYUP;
}

PhysicalKey KeyboardMappingEngine::physical_key(
    const KBDLLHOOKSTRUCT& event) noexcept {
    return {
        .virtual_key = event.vkCode,
        .scan_code = event.scanCode,
        .extended = (event.flags & LLKHF_EXTENDED) != 0,
    };
}

bool KeyboardMappingEngine::same_key(
    const PhysicalKey& left, const PhysicalKey& right) noexcept {
    return left == right;
}

bool KeyboardMappingEngine::rule_precedes(
    const CompiledRule& candidate,
    const CompiledRule& current) noexcept {
    const auto candidate_scoped = !candidate.process_name.empty();
    const auto current_scoped = !current.process_name.empty();
    if (candidate_scoped != current_scoped) return candidate_scoped;

    // exact_match has no observable meaning for a global rule because there
    // is no process scope to compare.  Only scoped rules participate in this
    // priority tier.
    if (candidate_scoped
        && candidate.rule.exact_match != current.rule.exact_match) {
        return candidate.rule.exact_match;
    }

    if (candidate_scoped) {
        const auto candidate_scope_length = process_scope_length(
            candidate.process_name);
        const auto current_scope_length = process_scope_length(
            current.process_name);
        if (candidate_scope_length != current_scope_length) {
            return candidate_scope_length > current_scope_length;
        }
    }

    const auto candidate_keys = trigger_key_count(candidate.rule.trigger);
    const auto current_keys = trigger_key_count(current.rule.trigger);
    if (candidate_keys != current_keys) return candidate_keys > current_keys;

    // Equal-priority rules should not depend on their insertion order.  The
    // remaining comparisons are only a stable content tie-breaker; validation
    // still rejects duplicate source definitions in one scope.
    if (candidate.process_name != current.process_name) {
        return candidate.process_name < current.process_name;
    }
    if (const auto comparison = compare_trigger_identity(
            candidate.rule.trigger, current.rule.trigger);
        comparison != 0) {
        return comparison < 0;
    }
    if (const auto comparison = compare_output_identity(
            candidate.rule.output, current.rule.output);
        comparison != 0) {
        return comparison < 0;
    }
    return false;
}

bool KeyboardMappingEngine::pressed_contains(
    const PhysicalKey& key) const noexcept {
    for (std::size_t index = 0; index < pressed_count_; ++index) {
        if (same_key(pressed_[index].key, key)) return true;
    }
    if (key.scan_code == 0) {
        for (std::size_t index = 0; index < pressed_count_; ++index) {
            if (pressed_[index].key.virtual_key == key.virtual_key) return true;
        }
    }
    return false;
}

bool KeyboardMappingEngine::all_pressed_are_source(
    const KeyboardTrigger& trigger) const noexcept {
    for (std::size_t index = 0; index < pressed_count_; ++index) {
        if (expected_trigger_key(trigger, pressed_[index].key)) continue;

        // A single-key mapping remains useful while an unrelated modifier is
        // held (for example, after a shortcut prefix has been replayed).  An
        // unrelated action key still disqualifies the rule so two ordinary
        // key streams cannot be merged accidentally.
        if (!trigger.single_key
            || !is_mapping_modifier(pressed_[index].key.virtual_key)) {
            return false;
        }
    }
    return true;
}

bool KeyboardMappingEngine::matches_rule(
    const CompiledRule& rule, const bool require_complete) const noexcept {
    if (!rule.rule.enabled) return false;
    if (!rule.process_name.empty()) {
        if (foreground_process_.empty()) return false;
        if (rule.rule.exact_match) {
            if (foreground_process_ != rule.process_name) return false;
        } else {
            // Non-exact scopes use a basename prefix. Keep this view-only so
            // the hook callback never allocates.
            const auto stem = [](const std::wstring& value) {
                const std::wstring_view view(value);
                return view.ends_with(L".exe")
                    ? view.substr(0, view.size() - 4) : view;
            };
            if (!stem(foreground_process_).starts_with(stem(rule.process_name))) {
                return false;
            }
        }
    }
    const auto& trigger = rule.rule.trigger;
    if (!is_mapping_key_valid(trigger.action)) return false;
    if (trigger.single_key && (trigger.modifier_count != 0 || trigger.chord_action)) {
        return false;
    }
    if (!all_pressed_are_source(trigger)) return false;
    if (trigger.single_key) {
        if (is_mapping_physical_modifier(trigger.action.virtual_key)) {
            return pressed_count_ == 1 && pressed_contains(trigger.action);
        }
        const auto pressed_modifier = [this](const UINT generic,
                                              const UINT left,
                                              const UINT right) noexcept {
            for (std::size_t index = 0; index < pressed_count_; ++index) {
                const auto virtual_key = pressed_[index].key.virtual_key;
                if (virtual_key == generic || virtual_key == left
                    || virtual_key == right) {
                    return true;
                }
            }
            return false;
        };
        const auto output_modifier = [&rule](const UINT generic,
                                             const UINT left,
                                             const UINT right) noexcept {
            const auto& output = rule.rule.output;
            if (output.single_key) return false;
            for (std::size_t index = 0; index < output.modifier_count; ++index) {
                const auto virtual_key = output.modifiers[index].virtual_key;
                if (virtual_key == generic || virtual_key == left
                    || virtual_key == right) {
                    return true;
                }
            }
            return false;
        };
        const auto win_down = pressed_modifier(0, VK_LWIN, VK_RWIN);
        const auto control_down = pressed_modifier(
            VK_CONTROL, VK_LCONTROL, VK_RCONTROL);
        const auto alt_down = pressed_modifier(VK_MENU, VK_LMENU, VK_RMENU);

        // Unrelated modifiers were already forwarded before this single-key
        // mapping began.  Never turn that live state into Win+L or a secure
        // attention sequence, and do not swallow the user's original secure
        // combination either.
        if ((trigger.action.virtual_key == L'L' && win_down)
            || (trigger.action.virtual_key == VK_DELETE
                && control_down && alt_down)) {
            return false;
        }
        const auto& output = rule.rule.output;
        const auto effective_win = win_down
            || output_modifier(0, VK_LWIN, VK_RWIN);
        const auto effective_control = control_down
            || output_modifier(VK_CONTROL, VK_LCONTROL, VK_RCONTROL);
        const auto effective_alt = alt_down
            || output_modifier(VK_MENU, VK_LMENU, VK_RMENU);
        if ((output.action.virtual_key == L'L' && effective_win)
            || (output.action.virtual_key == VK_DELETE
                && effective_control && effective_alt)) {
            return false;
        }

        // Modifiers are intentionally ignored for a single-key rule, but the
        // mapped action itself must be down.  The generic pressed-count check
        // below cannot be used here because it would count those unrelated
        // modifiers as additional source keys.
        return pressed_contains(trigger.action);
    }
    const auto expected = trigger_key_count(trigger);
    if (pressed_count_ == 0 || pressed_count_ > expected) return false;
    if (require_complete && pressed_count_ != expected) return false;

    // A shortcut with modifiers is only a prefix after at least one modifier
    // has been pressed.  This prevents an ordinary action key from being
    // delayed merely because a rule also contains that key.
    if (!require_complete && !trigger.single_key
        && trigger.modifier_count != 0
        ) {
        bool modifier_down = false;
        for (std::size_t index = 0; index < pressed_count_ && !modifier_down; ++index) {
            for (std::size_t modifier = 0;
                 modifier < trigger.modifier_count; ++modifier) {
                if (pressed_[index].key == trigger.modifiers[modifier]) {
                    modifier_down = true;
                    break;
                }
            }
        }
        if (!modifier_down) return false;
    }
    return true;
}

bool KeyboardMappingEngine::is_prefix(const CompiledRule& rule) const noexcept {
    if (pending_has_key_up()) return false;
    const auto& trigger = rule.rule.trigger;
    if (trigger.single_key) return false;
    const auto expected = trigger_key_count(trigger);
    return expected > 0 && pressed_count_ < expected
        && matches_rule(rule, false);
}

const KeyboardMappingEngine::CompiledRule*
KeyboardMappingEngine::select_match(const bool require_complete) const noexcept {
    const CompiledRule* selected = nullptr;
    for (const auto& rule : rules_) {
        if (!matches_rule(rule, require_complete)) continue;
        if (!selected || rule_precedes(rule, *selected)) {
            selected = &rule;
        }
    }
    return selected;
}

const KeyboardMappingEngine::CompiledRule*
KeyboardMappingEngine::select_prefix() const noexcept {
    const CompiledRule* selected = nullptr;
    for (const auto& rule : rules_) {
        if (!is_prefix(rule)) continue;
        if (!selected || rule_precedes(rule, *selected)) {
            selected = &rule;
        }
    }
    return selected;
}

bool KeyboardMappingEngine::append_pending(
    const UINT message, const KBDLLHOOKSTRUCT& event) noexcept {
    if (pending_count_ >= pending_events_.size()) {
        mark_diagnostic();
        return false;
    }
    if (pending_count_ == 0) {
        std::uint64_t now = 0;
        try {
            now = clock_ ? clock_() : default_clock();
        } catch (...) {
            mark_diagnostic();
        }
        pending_deadline_ = now + pending_timeout_ms;
    }
    pending_events_[pending_count_++] = {.message = message, .event = event};
    return true;
}

bool KeyboardMappingEngine::pending_contains_key(
    const PhysicalKey& key) const noexcept {
    for (std::size_t index = 0; index < pending_count_; ++index) {
        const auto pending_key = physical_key(pending_events_[index].event);
        if (same_key(pending_key, key)) {
            return true;
        }
    }
    // Some keyboard drivers report a different scan/extended tuple for the
    // matching key-up.  Accept a virtual-key fallback only when that virtual
    // key is unique in the buffered sequence, so sided physical keys remain
    // distinct when both are present.
    std::size_t candidate = no_index;
    for (std::size_t index = 0; index < pending_count_; ++index) {
        const auto pending_key = physical_key(pending_events_[index].event);
        if (pending_key.virtual_key != key.virtual_key) continue;
        if (candidate != no_index) return false;
        candidate = index;
    }
    return candidate != no_index;
}

bool KeyboardMappingEngine::pending_has_key_up() const noexcept {
    for (std::size_t index = 0; index < pending_count_; ++index) {
        if (is_key_up(pending_events_[index].message)
            || (pending_events_[index].event.flags & LLKHF_UP) != 0) {
            return true;
        }
    }
    return false;
}

void KeyboardMappingEngine::clear_pending() noexcept {
    pending_events_.fill({});
    pending_count_ = 0;
    pending_deadline_ = 0;
    pending_modifier_rule_index_.reset();
}

bool KeyboardMappingEngine::replay_pending() noexcept {
    if (pending_count_ == 0) return true;

    std::array<INPUT, 16> inputs{};
    const auto count = pending_count_;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& pending = pending_events_[index];
        const PhysicalKey key = physical_key(pending.event);
        fill_keyboard_input(
            inputs[index], key,
            !is_key_up(pending.message)
                && (pending.event.flags & LLKHF_UP) == 0,
            replay_injected_marker);
    }
    clear_pending();
    return send_events(inputs.data(), static_cast<UINT>(count));
}

bool KeyboardMappingEngine::send_output_down(
    const KeyboardOutput& output) noexcept {
    if (!is_mapping_key_valid(output.action)) return false;
    std::array<INPUT, 5> inputs{};
    UINT count = 0;
    if (!output.single_key) {
        for (std::size_t index = 0; index < output.modifier_count; ++index) {
            if (!is_mapping_key_valid(output.modifiers[index])) return false;
            fill_keyboard_input(inputs[count], output.modifiers[index], true,
                                target_injected_marker);
            ++count;
        }
    }
    const auto& action = output.action;
    fill_keyboard_input(inputs[count], action, true, target_injected_marker);
    ++count;
    return send_events(inputs.data(), count);
}

bool KeyboardMappingEngine::send_output_up(
    const KeyboardOutput& output) noexcept {
    if (!is_mapping_key_valid(output.action)) return false;
    std::array<INPUT, 5> inputs{};
    UINT count = 0;
    const auto append_up = [&inputs, &count](const PhysicalKey& key) {
        fill_keyboard_input(inputs[count++], key, false, target_injected_marker);
    };

    append_up(output.action);
    if (!output.single_key) {
        for (std::size_t index = output.modifier_count; index > 0; --index) {
            if (!is_mapping_key_valid(output.modifiers[index - 1])) return false;
            append_up(output.modifiers[index - 1]);
        }
    }
    // A low-level input sink can report a partial delivery.  Re-issuing the
    // complete key-up sequence is safe and ensures a target Win/Shift/Ctrl
    // modifier is not left logically down when the first batch stops early.
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (send_events(inputs.data(), count)) return true;
    }
    return false;
}

bool KeyboardMappingEngine::send_output_tap(
    const KeyboardOutput& output) noexcept {
    if (!send_output_down(output)) {
        (void)send_output_up(output);
        mark_diagnostic();
        return false;
    }
    if (!send_output_up(output)) {
        // A partial key-up failure can leave a target modifier logically down.
        // Retry the complete reverse sequence before the source is replayed.
        (void)send_output_up(output);
        mark_diagnostic();
        return false;
    }
    return true;
}

bool KeyboardMappingEngine::send_single(
    const PhysicalKey& key, const bool key_down, const ULONG_PTR marker) noexcept {
    if (!is_mapping_key_valid(key)) return false;
    INPUT input{};
    fill_keyboard_input(input, key, key_down, marker);
    return send_events(&input, 1);
}

bool KeyboardMappingEngine::send_events(
    INPUT* inputs, const UINT count) noexcept {
    if (count == 0) return true;
    try {
        if (!input_sink_) {
            mark_diagnostic();
            return false;
        }
        UINT sent = 0;
        for (int attempt = 0; sent < count && attempt < 3; ++attempt) {
            const auto result = input_sink_(inputs + sent, count - sent);
            if (result == 0) break;
            sent += std::min(result, count - sent);
        }
        if (sent != count) {
            mark_diagnostic();
            return false;
        }
    } catch (...) {
        mark_diagnostic();
        return false;
    }
    return true;
}

bool KeyboardMappingEngine::activate(const CompiledRule& rule) noexcept {
    auto slot = std::ranges::find_if(
        active_mappings_, [](const ActiveMapping& mapping) {
            return !mapping.active;
        });
    if (slot == active_mappings_.end()) {
        mark_diagnostic();
        return false;
    }

    ActiveMapping mapping{};
    mapping.rule_index = static_cast<std::size_t>(&rule - rules_.data());
    mapping.active = true;
    const auto& trigger = rule.rule.trigger;
    for (std::size_t index = 0; index < trigger.modifier_count; ++index) {
        mapping.source_keys[mapping.source_count++] = trigger.modifiers[index];
    }
    mapping.source_keys[mapping.source_count++] = trigger.action;
    if (trigger.chord_action) {
        mapping.source_keys[mapping.source_count++] = *trigger.chord_action;
    }

    if (!send_output_down(rule.rule.output)) {
        // SendInput may report a partial failure.  We cannot know which
        // individual event reached Windows, so issue the complete reverse
        // sequence as a best-effort cleanup before allowing the source to be
        // replayed.
        (void)send_output_up(rule.rule.output);
        mark_diagnostic();
        *slot = {};
        return false;
    }
    *slot = mapping;
    return true;
}

bool KeyboardMappingEngine::belongs_to_active(
    const PhysicalKey& key, const bool allow_virtual_fallback) const noexcept {
    for (const auto& mapping : active_mappings_) {
        if (!mapping.active) continue;
        for (std::size_t index = 0; index < mapping.source_count; ++index) {
            if (same_key(mapping.source_keys[index], key)) return true;
            if ((allow_virtual_fallback || key.scan_code == 0)
                && mapping.source_keys[index].virtual_key == key.virtual_key) {
                return true;
            }
        }
    }
    return false;
}

bool KeyboardMappingEngine::active_released() const noexcept {
    for (const auto& mapping : active_mappings_) {
        if (!mapping.active) continue;
        for (std::size_t index = 0; index < mapping.source_count; ++index) {
            if (pressed_contains(mapping.source_keys[index])) return false;
        }
    }
    return true;
}

void KeyboardMappingEngine::clear_pressed() noexcept {
    pressed_.fill({});
    pressed_count_ = 0;
}

void KeyboardMappingEngine::mark_pressed(
    const PhysicalKey& key, const bool down) noexcept {
    std::size_t found = no_index;
    for (std::size_t index = 0; index < pressed_count_; ++index) {
        if (same_key(pressed_[index].key, key)) {
            found = index;
            break;
        }
    }
    if (found == no_index && !down) {
        std::size_t candidate = no_index;
        for (std::size_t index = 0; index < pressed_count_; ++index) {
            if (pressed_[index].key.virtual_key != key.virtual_key) continue;
            if (candidate != no_index) {
                candidate = no_index;
                break;
            }
            candidate = index;
        }
        found = candidate;
    }

    if (down) {
        if (found != no_index) {
            pressed_[found].down = true;
            return;
        }
        if (pressed_count_ >= pressed_.size()) {
            mark_diagnostic();
            return;
        }
        pressed_[pressed_count_++] = {.key = key, .down = true};
        return;
    }

    if (found == no_index) return;
    const auto last = pressed_count_ - 1;
    if (found != last) pressed_[found] = pressed_[last];
    pressed_[last] = {};
    --pressed_count_;
}

MappingEventResult KeyboardMappingEngine::handle(
    const UINT message, const KBDLLHOOKSTRUCT& event) noexcept {
    MappingEventResult result{};
    if (!is_key_down(message) && !is_key_up(message)) return result;

    // All injected events are outside the physical-key stream owned by this
    // engine.  Simpilot's target and replay events carry private markers, but
    // arbitrary SendInput traffic must also bypass remapping to avoid
    // intercepting another component's synthetic input or creating loops.
    if ((event.flags & LLKHF_INJECTED) != 0
        || event.dwExtraInfo == target_injected_marker
        || event.dwExtraInfo == replay_injected_marker) {
        return result;
    }
    if (!enabled_ || rules_.empty()) return result;

    const auto key = physical_key(event);
    if (!is_mapping_key_valid(key)) return result;

    const auto down = is_key_down(message);
    const auto was_pressed = pressed_contains(key);
    const auto was_pending = pending_contains_key(key);
    const auto was_active = belongs_to_active(key, !down);
    mark_pressed(key, down);

    const auto pending_modifier_matches = [this, &key]() noexcept {
        if (!pending_modifier_rule_index_
            || *pending_modifier_rule_index_ >= rules_.size()) {
            return false;
        }
        const auto& action = rules_[*pending_modifier_rule_index_]
            .rule.trigger.action;
        return same_key(action, key)
            || action.virtual_key == key.virtual_key;
    };

    // A physical modifier can be used as a single source key, but its initial
    // key-down must remain reversible so ordinary shortcuts such as Ctrl+C
    // still work. A release before the deadline commits a one-shot target;
    // any different key invalidates the single-modifier candidate and lets
    // the normal prefix machinery replay or complete the longer shortcut.
    if (!down && was_pending && pending_modifier_matches()) {
        const auto rule_index = *pending_modifier_rule_index_;
        if (!append_pending(message, event)) {
            (void)replay_pending();
            result.diagnostic = true;
            result.decision = MappingEventDecision::pass;
            return result;
        }
        if (rule_index < rules_.size()
            && send_output_tap(rules_[rule_index].rule.output)) {
            clear_pending();
            result.decision = MappingEventDecision::suppress;
            return result;
        }
        const auto replayed = replay_pending();
        result.diagnostic = true;
        result.decision = replayed
            ? MappingEventDecision::suppress : MappingEventDecision::pass;
        return result;
    }
    if (down && pending_modifier_rule_index_
        && !pending_modifier_matches()) {
        pending_modifier_rule_index_.reset();
    }

    // A prefix can be disproved by the current event.  Once the buffered
    // events are replayed, a genuinely new key-down should still get a chance
    // to start its own mapping; otherwise a mapping such as Q->W would be
    // skipped whenever Q arrives immediately after an unrelated prefix.
    const auto replay_pending_and_retry = [this, &key, &event, message,
                                            down, was_pressed]() noexcept {
        (void)replay_pending();
        if (down && !was_pressed) {
            mark_pressed(key, false);
            return handle(message, event);
        }
        return MappingEventResult{};
    };

    if (was_active) {
        if (down) {
            if (was_pressed && !is_mapping_modifier(key.virtual_key)) {
                for (const auto& mapping : active_mappings_) {
                    if (!mapping.active || mapping.rule_index >= rules_.size()) {
                        continue;
                    }
                    if (!send_single(
                            rules_[mapping.rule_index].rule.output.action,
                            true, target_injected_marker)) {
                        mark_diagnostic();
                    }
                    break;
                }
            }
            result.decision = MappingEventDecision::suppress;
            return result;
        }
        for (auto& mapping : active_mappings_) {
            if (!mapping.active) continue;
            bool source_still_down = false;
            for (std::size_t index = 0; index < mapping.source_count; ++index) {
                if (pressed_contains(mapping.source_keys[index])) {
                    source_still_down = true;
                    break;
                }
            }
            if (source_still_down) continue;
            if (mapping.rule_index < rules_.size()
                && !send_output_up(rules_[mapping.rule_index].rule.output)) {
                mark_diagnostic();
            }
            mapping = {};
        }
        result.decision = MappingEventDecision::suppress;
        return result;
    }

    if (!down) {
        if (pending_count_ == 0) return result;
        if (!was_pending) {
            // A key that was already held before the prefix started is not
            // part of the candidate sequence. Let its release pass through.
            return result;
        }
        if (!append_pending(message, event)) {
            (void)replay_pending();
            result.decision = MappingEventDecision::pass;
            return result;
        }
        if (select_prefix()) {
            result.decision = MappingEventDecision::suppress;
            return result;
        }
        result.decision = replay_pending()
            ? MappingEventDecision::suppress : MappingEventDecision::pass;
        return result;
    }

    if (pending_count_ != 0 && was_pressed && was_pending) {
        // Auto-repeat must stay suppressed but must not fill the bounded
        // replay queue with duplicate key-down events.
        result.decision = MappingEventDecision::suppress;
        return result;
    }

    if (pending_count_ != 0 && pending_has_key_up()) {
        auto retry = replay_pending_and_retry();
        if (retry.decision != MappingEventDecision::pass
            || retry.diagnostic) {
            return retry;
        }
        result.decision = MappingEventDecision::pass;
        return result;
    }

    const auto complete = select_match(true);
    // A multi-key mapping may only take ownership after its prefix has been
    // buffered.  If the action key was pressed first, its key-down has already
    // been delivered to the target application; activating at the later
    // modifier key-down would swallow that key's release and leave the target
    // in a stuck state.
    if (complete && complete->rule.trigger.single_key
        && is_mapping_physical_modifier(
            complete->rule.trigger.action.virtual_key)
        && pending_count_ == 0 && !was_pressed) {
        if (!append_pending(message, event)) {
            result.diagnostic = true;
            return result;
        }
        pending_modifier_rule_index_ = static_cast<std::size_t>(
            complete - rules_.data());
        result.decision = MappingEventDecision::suppress;
        return result;
    }

    const auto can_activate = complete
        && ((!complete->rule.trigger.single_key && pending_count_ != 0)
            || (complete->rule.trigger.single_key && !was_pressed));
    if (can_activate) {
        if (complete->rule.trigger.single_key && pending_count_ != 0
            && !replay_pending()) {
            // The buffered prefix was already consumed by the hook.  Do not
            // activate a new mapping after replay failed; let the current
            // key continue normally while exposing the diagnostic state.
            result.diagnostic = true;
            result.decision = MappingEventDecision::pass;
            return result;
        }
        if (!activate(*complete)) {
            // The source was not forwarded yet, so a failed output injection
            // is still best-effort replayable.
            (void)replay_pending();
            result.diagnostic = true;
            return result;
        }
        clear_pending();
        result.decision = MappingEventDecision::suppress;
        return result;
    }

    if (select_prefix()) {
        if (append_pending(message, event)) {
            result.decision = MappingEventDecision::suppress;
            return result;
        }
        auto retry = replay_pending_and_retry();
        if (retry.decision != MappingEventDecision::pass
            || retry.diagnostic) {
            return retry;
        }
        result.decision = MappingEventDecision::pass;
        return result;
    }

    if (pending_count_ != 0) {
        // The buffered prefix has already been consumed by this engine.  Replay
        // it and retry a new key-down so an independent mapping can still win.
        auto retry = replay_pending_and_retry();
        if (retry.decision != MappingEventDecision::pass
            || retry.diagnostic) {
            return retry;
        }
        result.decision = MappingEventDecision::pass;
    }
    return result;
}

void KeyboardMappingEngine::on_timer() noexcept {
    if (pending_count_ == 0) return;
    std::uint64_t now = 0;
    try {
        now = clock_ ? clock_() : default_clock();
    } catch (...) {
        mark_diagnostic();
        (void)replay_pending();
        return;
    }
    if (now < pending_deadline_) return;

    if (pending_modifier_rule_index_) {
        const auto rule_index = *pending_modifier_rule_index_;
        if (rule_index < rules_.size()
            && pressed_contains(rules_[rule_index].rule.trigger.action)) {
            if (activate(rules_[rule_index])) {
                clear_pending();
                return;
            }
            (void)replay_pending();
            return;
        }
    }
    (void)replay_pending();
}

void KeyboardMappingEngine::set_foreground_process(
    std::wstring process_name) noexcept {
    try {
        auto normalized = normalize_mapping_process_name(std::move(process_name));
        if (normalized == foreground_process_) return;
        if (pending_count_ != 0) (void)replay_pending();
        foreground_process_ = std::move(normalized);
    } catch (...) {
        mark_diagnostic();
        if (pending_count_ != 0) (void)replay_pending();
    }
}

void KeyboardMappingEngine::reset(const bool replay_pending_events) noexcept {
    if (replay_pending_events) (void)replay_pending();
    for (auto& mapping : active_mappings_) {
        if (!mapping.active) continue;
        if (mapping.rule_index < rules_.size()
            && !send_output_up(rules_[mapping.rule_index].rule.output)) {
            mark_diagnostic();
        }
        mapping = {};
    }
    clear_pending();
    clear_pressed();
}

bool KeyboardMappingEngine::consume_diagnostic() noexcept {
    const auto result = diagnostic_;
    diagnostic_ = false;
    return result;
}

} // namespace simpilot
