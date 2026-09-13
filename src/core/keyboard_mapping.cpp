#include "simpilot/keyboard_mapping.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <map>
#include <ranges>
#include <set>
#include <sstream>

namespace simpilot {
namespace {

std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    const auto last = value.find_last_not_of(L" \t\r\n");
    return first == std::wstring::npos ? std::wstring{}
                                      : value.substr(first, last - first + 1);
}

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

bool same_key(const PhysicalKey& left, const PhysicalKey& right) noexcept {
    return left == right;
}

bool key_less(const PhysicalKey& left, const PhysicalKey& right) noexcept {
    if (left.virtual_key != right.virtual_key) {
        return left.virtual_key < right.virtual_key;
    }
    if (left.scan_code != right.scan_code) {
        return left.scan_code < right.scan_code;
    }
    return left.extended < right.extended;
}

template <typename Candidate>
std::size_t modifier_count(const Candidate& candidate) noexcept {
    return std::min(candidate.modifier_count, candidate.modifiers.size());
}

bool contains_modifier(const KeyboardTrigger& trigger, const UINT virtual_key) noexcept {
    for (std::size_t index = 0; index < modifier_count(trigger); ++index) {
        if (trigger.modifiers[index].virtual_key == virtual_key) return true;
    }
    return false;
}

bool contains_modifier(const KeyboardOutput& output, const UINT virtual_key) noexcept {
    for (std::size_t index = 0; index < modifier_count(output); ++index) {
        if (output.modifiers[index].virtual_key == virtual_key) return true;
    }
    return false;
}

bool has_win_l(const KeyboardTrigger& trigger) noexcept {
    const auto win = contains_modifier(trigger, VK_LWIN)
        || contains_modifier(trigger, VK_RWIN);
    return win && (trigger.action.virtual_key == L'L'
        || (trigger.chord_action && trigger.chord_action->virtual_key == L'L'));
}

bool has_win_l(const KeyboardOutput& output) noexcept {
    const auto win = contains_modifier(output, VK_LWIN)
        || contains_modifier(output, VK_RWIN);
    return win && output.action.virtual_key == L'L';
}

template <typename Candidate>
bool has_modifier(const Candidate& candidate, const UINT virtual_key) noexcept {
    return contains_modifier(candidate, virtual_key)
        || (virtual_key == VK_CONTROL
            && (contains_modifier(candidate, VK_LCONTROL)
                || contains_modifier(candidate, VK_RCONTROL)))
        || (virtual_key == VK_MENU
            && (contains_modifier(candidate, VK_LMENU)
                || contains_modifier(candidate, VK_RMENU)));
}

bool has_secure_attention_sequence(const KeyboardTrigger& trigger) noexcept {
    if (!has_modifier(trigger, VK_CONTROL) || !has_modifier(trigger, VK_MENU)) {
        return false;
    }
    return trigger.action.virtual_key == VK_DELETE
        || (trigger.chord_action && trigger.chord_action->virtual_key == VK_DELETE);
}

bool has_secure_attention_sequence(const KeyboardOutput& output) noexcept {
    return has_modifier(output, VK_CONTROL) && has_modifier(output, VK_MENU)
        && output.action.virtual_key == VK_DELETE;
}

std::wstring key_token(const PhysicalKey& key) {
    return std::to_wstring(key.virtual_key) + L":"
        + std::to_wstring(key.scan_code) + L":"
        + (key.extended ? L"1" : L"0");
}

std::wstring trigger_token(const KeyboardTrigger& trigger) {
    std::wstring result = trigger.single_key ? L"K:" : L"S:";
    std::array<PhysicalKey, 4> modifiers{};
    const auto count = modifier_count(trigger);
    std::ranges::copy_n(trigger.modifiers.begin(), count, modifiers.begin());
    std::ranges::sort(modifiers.begin(), modifiers.begin()
        + static_cast<std::ptrdiff_t>(count), key_less);
    for (std::size_t index = 0; index < count; ++index) {
        result.append(key_token(modifiers[index])).push_back(L'|');
    }
    auto first_action = trigger.action;
    auto second_action = trigger.chord_action;
    if (second_action && key_less(*second_action, first_action)) {
        std::swap(first_action, *second_action);
    }
    result.append(key_token(first_action));
    if (second_action) {
        result.push_back(L'|');
        result.append(key_token(*second_action));
    }
    return result;
}

std::wstring output_token(const KeyboardOutput& output) {
    std::wstring result = output.single_key ? L"K:" : L"S:";
    std::array<PhysicalKey, 4> modifiers{};
    const auto count = modifier_count(output);
    std::ranges::copy_n(output.modifiers.begin(), count, modifiers.begin());
    std::ranges::sort(modifiers.begin(), modifiers.begin()
        + static_cast<std::ptrdiff_t>(count), key_less);
    for (std::size_t index = 0; index < count; ++index) {
        result.append(key_token(modifiers[index])).push_back(L'|');
    }
    result.append(key_token(output.action));
    return result;
}

bool duplicate_modifiers(const KeyboardTrigger& trigger) noexcept {
    const auto count = modifier_count(trigger);
    for (std::size_t left = 0; left < count; ++left) {
        for (std::size_t right = left + 1; right < count; ++right) {
            if (same_key(trigger.modifiers[left], trigger.modifiers[right])) return true;
        }
    }
    return false;
}

bool duplicate_modifiers(const KeyboardOutput& output) noexcept {
    const auto count = modifier_count(output);
    for (std::size_t left = 0; left < count; ++left) {
        for (std::size_t right = left + 1; right < count; ++right) {
            if (same_key(output.modifiers[left], output.modifiers[right])) return true;
        }
    }
    return false;
}

bool contains_physical_key(
    const KeyboardTrigger& trigger, const PhysicalKey& key) noexcept {
    for (std::size_t index = 0; index < modifier_count(trigger); ++index) {
        if (same_key(trigger.modifiers[index], key)) return true;
    }
    if (same_key(trigger.action, key)) return true;
    return trigger.chord_action && same_key(*trigger.chord_action, key);
}

bool trigger_is_reachable_prefix(
    const KeyboardTrigger& prefix, const KeyboardTrigger& candidate) noexcept {
    if (candidate.single_key
        || prefix.modifier_count > prefix.modifiers.size()
        || candidate.modifier_count > candidate.modifiers.size()) {
        return false;
    }

    // Single-key mappings and modified shortcuts have deterministic priority,
    // but a bare two-action chord would otherwise depend on which action key
    // happened to arrive first.
    if (prefix.single_key) {
        return candidate.modifier_count == 0 && candidate.chord_action
            && contains_physical_key(candidate, prefix.action);
    }

    if (!contains_physical_key(candidate, prefix.action)) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.modifier_count; ++index) {
        if (!contains_physical_key(candidate, prefix.modifiers[index])) return false;
    }
    if (prefix.chord_action && !contains_physical_key(
            candidate, *prefix.chord_action)) {
        return false;
    }
    const auto prefix_count = prefix.modifier_count + 1
        + (prefix.chord_action ? 1 : 0);
    const auto candidate_count = candidate.modifier_count + 1
        + (candidate.chord_action ? 1 : 0);
    return candidate_count > prefix_count;
}

std::wstring_view process_stem(const std::wstring_view process_name) noexcept {
    constexpr std::wstring_view suffix = L".exe";
    return process_name.ends_with(suffix)
        ? process_name.substr(0, process_name.size() - suffix.size())
        : process_name;
}

bool process_scopes_overlap(
    const KeyboardMappingRule& left,
    const KeyboardMappingRule& right) {
    const auto left_name = normalize_mapping_process_name(left.process_name);
    const auto right_name = normalize_mapping_process_name(right.process_name);
    if (left_name.empty() || right_name.empty()) return true;
    if (left.exact_match && right.exact_match) return left_name == right_name;
    if (left.exact_match) {
        return process_stem(left_name).starts_with(process_stem(right_name));
    }
    if (right.exact_match) {
        return process_stem(right_name).starts_with(process_stem(left_name));
    }
    const auto left_stem = process_stem(left_name);
    const auto right_stem = process_stem(right_name);
    return left_stem.starts_with(right_stem)
        || right_stem.starts_with(left_stem);
}

std::wstring process_scope_token(const KeyboardMappingRule& rule) {
    const auto process_name = normalize_mapping_process_name(rule.process_name);
    if (process_name.empty()) return L"global";
    return (rule.exact_match ? L"exact:" : L"prefix:") + process_name;
}

} // namespace

bool is_mapping_modifier(const UINT virtual_key) noexcept {
    return virtual_key == VK_CONTROL || virtual_key == VK_LCONTROL
        || virtual_key == VK_RCONTROL || virtual_key == VK_SHIFT
        || virtual_key == VK_LSHIFT || virtual_key == VK_RSHIFT
        || virtual_key == VK_MENU || virtual_key == VK_LMENU
        || virtual_key == VK_RMENU || virtual_key == VK_LWIN
        || virtual_key == VK_RWIN;
}

bool is_mapping_key_valid(const PhysicalKey& key) noexcept {
    if (key.virtual_key == 0 || key.virtual_key >= 0xFF) return false;
    if (key.virtual_key == VK_PACKET || key.virtual_key == VK_PROCESSKEY) return false;
    // The low-level hook normally reports an 8-bit make code, but extended
    // keys are also represented by the 0x100+ form by some producers.  The
    // persisted/input representation is a WORD, so accept the full range
    // that can be carried losslessly through INPUT::ki.wScan.
    if (key.scan_code > 0xFFFF) return false;
    return true;
}

std::wstring normalize_mapping_process_name(std::wstring value) {
    value = lowercase(trim(std::move(value)));
    if (value.empty()) return {};
    if (value.find_first_of(L"\\/:") != std::wstring::npos) return {};
    if (!value.ends_with(L".exe")) value.append(L".exe");
    return value;
}

std::wstring format_mapping_key(const PhysicalKey& key) {
    if (key.empty()) return L"";
    switch (key.virtual_key) {
    case VK_LCONTROL: return L"Left Ctrl";
    case VK_RCONTROL: return L"Right Ctrl";
    case VK_LMENU: return L"Left Alt";
    case VK_RMENU: return L"Right Alt";
    case VK_LSHIFT: return L"Left Shift";
    case VK_RSHIFT: return L"Right Shift";
    case VK_LWIN: return L"Left Win";
    case VK_RWIN: return L"Right Win";
    default: break;
    }
    auto scan_code = key.scan_code == 0
        ? MapVirtualKeyW(key.virtual_key, MAPVK_VK_TO_VSC)
        : key.scan_code;
    auto extended = key.extended;
    if ((scan_code & 0xFF00U) == 0x0100U) {
        scan_code &= 0x00FFU;
        extended = true;
    }
    const auto scan = (scan_code << 16U)
        | (extended ? (1U << 24U) : 0U);
    wchar_t buffer[64]{};
    if (GetKeyNameTextW(static_cast<LONG>(scan), buffer,
                        static_cast<int>(std::size(buffer))) > 0) {
        return buffer;
    }
    return L"VK" + std::to_wstring(key.virtual_key);
}

std::wstring format_mapping_trigger(const KeyboardTrigger& trigger) {
    if (trigger.single_key) return format_mapping_key(trigger.action);
    std::wstring result;
    for (std::size_t index = 0; index < modifier_count(trigger); ++index) {
        if (!result.empty()) result.push_back(L'+');
        result.append(format_mapping_key(trigger.modifiers[index]));
    }
    if (!result.empty()) result.push_back(L'+');
    result.append(format_mapping_key(trigger.action));
    if (trigger.chord_action) {
        result.append(L" + ").append(format_mapping_key(*trigger.chord_action));
    }
    return result;
}

std::wstring format_mapping_output(const KeyboardOutput& output) {
    if (output.single_key) return format_mapping_key(output.action);
    std::wstring result;
    for (std::size_t index = 0; index < modifier_count(output); ++index) {
        if (!result.empty()) result.push_back(L'+');
        result.append(format_mapping_key(output.modifiers[index]));
    }
    if (!result.empty()) result.push_back(L'+');
    result.append(format_mapping_key(output.action));
    return result;
}

std::vector<KeyboardMappingValidationError> validate_keyboard_mappings(
    const std::vector<KeyboardMappingRule>& rules) {
    std::vector<KeyboardMappingValidationError> errors;
    if (rules.size() > 128) {
        errors.push_back({rules.size(), L"At most 128 keyboard mappings are supported."});
    }

    std::map<std::wstring, std::size_t> source_owners;
    std::vector<std::wstring> sources;
    std::vector<std::wstring> targets;
    for (std::size_t index = 0; index < rules.size(); ++index) {
        const auto& rule = rules[index];
        const auto add_error = [&errors, index](std::wstring message) {
            errors.push_back({index, std::move(message)});
        };
        if (!rule.process_name.empty()
            && normalize_mapping_process_name(rule.process_name).empty()) {
            add_error(L"Process scope must be an executable base name.");
        }

        const auto& trigger = rule.trigger;
        if (trigger.modifier_count > trigger.modifiers.size()) {
            add_error(L"Too many source modifiers.");
        }
        if (trigger.single_key) {
            if (trigger.modifier_count != 0 || trigger.chord_action) {
                add_error(L"A single-key source cannot contain modifiers or a chord action.");
            }
            if (!is_mapping_key_valid(trigger.action)
                || is_mapping_modifier(trigger.action.virtual_key)) {
                add_error(L"Source key is invalid.");
            }
        } else {
            if (trigger.chord_action) {
                if (trigger.modifier_count > 3) {
                    add_error(L"A chord source supports at most three modifiers.");
                }
            } else if (trigger.modifier_count == 0
                       || trigger.modifier_count > 4) {
                add_error(L"A shortcut source needs one to four modifiers.");
            }
            if (!is_mapping_key_valid(trigger.action)
                || is_mapping_modifier(trigger.action.virtual_key)) {
                add_error(L"Source action must be a non-modifier key.");
            }
            for (std::size_t modifier = 0;
                 modifier < modifier_count(trigger); ++modifier) {
                if (!is_mapping_key_valid(trigger.modifiers[modifier])
                    || !is_mapping_modifier(trigger.modifiers[modifier].virtual_key)) {
                    add_error(L"Source modifier is invalid.");
                }
            }
            if (duplicate_modifiers(trigger)) add_error(L"Source modifiers repeat.");
            if (trigger.chord_action
                && (!is_mapping_key_valid(*trigger.chord_action)
                    || is_mapping_modifier(trigger.chord_action->virtual_key)
                    || same_key(*trigger.chord_action, trigger.action))) {
                add_error(L"Chord action is invalid or repeats the first action.");
            }
        }
        if (has_win_l(trigger)) add_error(L"Win+L cannot be remapped.");

        const auto& output = rule.output;
        if (output.modifier_count > output.modifiers.size()) {
            add_error(L"Too many target modifiers.");
        }
        if (output.single_key) {
            if (output.modifier_count != 0) {
                add_error(L"A single-key target cannot contain modifiers.");
            }
            if (!is_mapping_key_valid(output.action)
                || is_mapping_modifier(output.action.virtual_key)) {
                add_error(L"Target key is invalid.");
            }
        } else {
            if (output.modifier_count == 0
                || output.modifier_count > output.modifiers.size()) {
                add_error(L"A shortcut target needs one to four modifiers.");
            }
            if (!is_mapping_key_valid(output.action)
                || is_mapping_modifier(output.action.virtual_key)) {
                add_error(L"Target action is invalid.");
            }
            for (std::size_t modifier = 0;
                 modifier < modifier_count(output); ++modifier) {
                if (!is_mapping_key_valid(output.modifiers[modifier])
                    || !is_mapping_modifier(output.modifiers[modifier].virtual_key)) {
                    add_error(L"Target modifier is invalid.");
                }
            }
            if (duplicate_modifiers(output)) add_error(L"Target modifiers repeat.");
        }
        if (has_win_l(output)) add_error(L"Win+L cannot be generated.");
        if (has_secure_attention_sequence(trigger)) {
            add_error(L"Secure attention sequences cannot be remapped.");
        }
        if (has_secure_attention_sequence(output)) {
            add_error(L"Secure attention sequences cannot be generated.");
        }

        const auto source = trigger_token(trigger);
        const auto key = process_scope_token(rule) + L"\n" + source;
        if (const auto [found, inserted] = source_owners.emplace(key, index);
            !inserted) {
            // Disabled entries are validated too, so enabling a rule later
            // cannot make validity depend on edit order.
            add_error(L"Duplicate source mapping in the same scope.");
        }
        sources.push_back(source);
        targets.push_back(output_token(output));
    }

    for (std::size_t left = 0; left < rules.size(); ++left) {
        if (!rules[left].enabled) continue;
        for (std::size_t right = left + 1; right < rules.size(); ++right) {
            if (!rules[right].enabled) continue;
            if (!process_scopes_overlap(rules[left], rules[right])) continue;
            if (trigger_is_reachable_prefix(
                    rules[left].trigger, rules[right].trigger)) {
                errors.push_back({left,
                    L"Shortcut sources contain an ambiguous prefix."});
            } else if (trigger_is_reachable_prefix(
                           rules[right].trigger, rules[left].trigger)) {
                errors.push_back({right,
                    L"Shortcut sources contain an ambiguous prefix."});
            }
        }
    }

    std::vector<int> visit(rules.size(), 0);
    const auto visit_rule = [&](const auto& self, const std::size_t index) -> bool {
        if (!rules[index].enabled) return false;
        if (visit[index] == 1) return true;
        if (visit[index] == 2) return false;
        visit[index] = 1;
        for (std::size_t next = 0; next < rules.size(); ++next) {
            if (!rules[next].enabled || targets[index] != sources[next]
                || !process_scopes_overlap(rules[index], rules[next])) {
                continue;
            }
            if (self(self, next)) return true;
        }
        visit[index] = 2;
        return false;
    };
    for (std::size_t index = 0; index < rules.size(); ++index) {
        if (visit_rule(visit_rule, index)) {
            errors.push_back({index, L"Mapping cycle is not allowed."});
        }
    }
    return errors;
}

} // namespace simpilot
