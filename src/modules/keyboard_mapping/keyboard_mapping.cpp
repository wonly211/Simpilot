#include "keyboard_mapping.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <iomanip>
#include <map>
#include <ranges>
#include <set>
#include <sstream>

namespace simpilot {
namespace {

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

std::optional<std::wstring> overlapping_process(
    const KeyboardMappingRule& left,
    const KeyboardMappingRule& right) {
    // Each name is an exact point or the open remainder of a prefix region.
    // The empty region represents a known process outside all listed names.
    std::set<std::pair<std::wstring, bool>> regions{{L"", false}};
    for (const auto* rule : {&left, &right}) for (const auto& name : rule->process_names)
        regions.emplace(name, rule->exact_match);
    const auto active = [](const KeyboardMappingRule& rule, const auto& process, bool exact) {
        if (mapping_application_scope(rule) == ApplicationScope::all) return true;
        const auto listed = std::ranges::any_of(rule.process_names, [&](const auto& name) {
            return rule.exact_match ? exact && process == name
                : !process.empty() && process_stem(process).starts_with(process_stem(name));
        });
        return rule.application_scope == ApplicationScope::excluded ? !listed : listed;
    };
    for (const auto& [process, exact] : regions)
        if (active(left, process, exact) && active(right, process, exact)) return process;
    return std::nullopt;
}

std::wstring process_scope_token(const std::wstring& name, const bool exact) {
    if (name.empty()) return L"global";
    return (exact ? L"exact:" : L"prefix:") + name;
}

} // namespace

ApplicationScope mapping_application_scope(const KeyboardMappingRule& rule) noexcept {
    return rule.application_scope == ApplicationScope::excluded ? ApplicationScope::excluded
        : rule.process_names.empty() ? ApplicationScope::all : ApplicationScope::included;
}

bool mapping_applies_to_process(const KeyboardMappingRule& rule, std::wstring_view process) noexcept {
    if (mapping_application_scope(rule) == ApplicationScope::all) return true;
    if (process.empty()) return false;
    const bool listed = std::ranges::any_of(rule.process_names, [&](const auto& name) {
        return rule.exact_match ? process == name : process_stem(process).starts_with(process_stem(name));
    });
    return rule.application_scope == ApplicationScope::excluded ? !listed : listed;
}

std::optional<std::vector<std::wstring>> normalize_mapping_process_names(
    const std::vector<std::wstring>& names) {
    if (names.size() > maximum_mapping_applications) return std::nullopt;
    std::vector<std::wstring> result;
    for (const auto& name : names) {
        auto normalized = normalize_mapping_process_name(name);
        if (normalized.empty() || normalized.size() > 255
            || normalized.find_first_of(L"<>\"|?*") != std::wstring::npos
            || std::ranges::any_of(normalized, [](const wchar_t c) { return c < 32; })) {
            return std::nullopt;
        }
        result.push_back(std::move(normalized));
    }
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<KeyboardMappingValidationError> validate_keyboard_mappings(
    const std::vector<KeyboardMappingRule>& input) {
    std::vector<KeyboardMappingValidationError> errors;
    if (input.size() > maximum_keyboard_mappings) {
        return {{input.size(), L"At most 128 keyboard mappings are supported."}};
    }
    auto rules = input;
    for (std::size_t index = 0; index < rules.size(); ++index) {
        auto names = normalize_mapping_process_names(rules[index].process_names);
        if (!names || (rules[index].application_scope == ApplicationScope::excluded && names->empty())
            || (rules[index].application_scope == ApplicationScope::all && !names->empty())) {
            errors.push_back({index, L"Use at most 32 nonempty executable base names."});
        } else {
            rules[index].process_names = std::move(*names);
        }
    }
    if (!errors.empty()) return errors;

    std::map<std::wstring, std::size_t> source_owners;
    std::vector<std::wstring> sources;
    std::vector<std::wstring> targets;
    for (std::size_t index = 0; index < rules.size(); ++index) {
        const auto& rule = rules[index];
        const auto add_error = [&errors, index](std::wstring message) {
            errors.push_back({index, std::move(message)});
        };
        const auto& trigger = rule.trigger;
        if (trigger.modifier_count > trigger.modifiers.size()) {
            add_error(L"Too many source modifiers.");
        }
        if (trigger.single_key) {
            if (trigger.modifier_count != 0 || trigger.chord_action) {
                add_error(L"A single-key source cannot contain modifiers or a chord action.");
            }
            if (!is_mapping_key_valid(trigger.action)
                || (is_mapping_modifier(trigger.action.virtual_key)
                    && !is_mapping_physical_modifier(
                        trigger.action.virtual_key))) {
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
                || (is_mapping_modifier(output.action.virtual_key)
                    && !is_mapping_physical_modifier(output.action.virtual_key))) {
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
        const auto register_scope = [&](const std::wstring& process) {
            const auto key = process_scope_token(process, rule.exact_match) + L"\n" + source;
            if (!source_owners.emplace(key, index).second) {
                // Validate disabled duplicates too, independent of edit order.
                errors.push_back({index, L"Duplicate source mapping in the same scope.",
                    KeyboardMappingValidationKind::duplicate_source, process});
            }
        };
        if (rule.application_scope != ApplicationScope::excluded) {
            if (rule.process_names.empty()) register_scope({});
            for (const auto& process : rule.process_names) register_scope(process);
        }
        sources.push_back(source);
        targets.push_back(output_token(output));
    }

    for (std::size_t left = 0; left < rules.size(); ++left) {
        for (std::size_t right = left + 1; right < rules.size(); ++right) {
            const auto process = overlapping_process(rules[left], rules[right]);
            if (!process) continue;
            if (rules[left].application_scope == ApplicationScope::excluded
                && rules[right].application_scope == ApplicationScope::excluded
                && sources[left] == sources[right] && targets[left] != targets[right]) {
                errors.push_back({right, L"Overlapping exclusion rules have different targets.",
                    KeyboardMappingValidationKind::duplicate_source, *process});
            }
            if (!rules[left].enabled || !rules[right].enabled) continue;
            const auto modifier_remap_conflicts = [](
                const KeyboardMappingRule& remap, const KeyboardMappingRule& shortcut) {
                return remap.trigger.single_key
                    && is_mapping_physical_modifier(remap.trigger.action.virtual_key)
                    && remap.output.single_key
                    && is_mapping_physical_modifier(remap.output.action.virtual_key)
                    && !shortcut.trigger.single_key
                    && contains_physical_key(shortcut.trigger, remap.trigger.action);
            };
            if (modifier_remap_conflicts(rules[left], rules[right])
                || modifier_remap_conflicts(rules[right], rules[left])) {
                errors.push_back({left,
                    L"A modifier remap conflicts with a shortcut using the same source modifier.",
                    KeyboardMappingValidationKind::modifier_conflict, *process});
            } else if (trigger_is_reachable_prefix(
                    rules[left].trigger, rules[right].trigger)) {
                errors.push_back({left,
                    L"Shortcut sources contain an ambiguous prefix.",
                    KeyboardMappingValidationKind::prefix_conflict, *process});
            } else if (trigger_is_reachable_prefix(
                           rules[right].trigger, rules[left].trigger)) {
                errors.push_back({right,
                    L"Shortcut sources contain an ambiguous prefix.",
                    KeyboardMappingValidationKind::prefix_conflict, *process});
            }
        }
    }

    if (!errors.empty()) return errors;
    // Exact names and prefix-only regions cover every distinct foreground
    // scope. A cycle must exist within one region, not just pairwise overlaps
    // between app lists (e.g. {A,B}, {B,C}, {A,C}).
    std::set<std::pair<std::wstring, bool>> contexts{{L"", false}};
    std::vector<std::vector<std::size_t>> edges(rules.size());
    for (std::size_t index = 0; index < rules.size(); ++index) {
        if (!rules[index].enabled) continue;
        for (const auto& process : rules[index].process_names) {
            contexts.emplace(process, rules[index].exact_match);
        }
        for (std::size_t next = 0; next < rules.size(); ++next) {
            if (rules[next].enabled && targets[index] == sources[next]) {
                edges[index].push_back(next);
            }
        }
    }
    for (const auto& [process, exact] : contexts) {
        std::vector<bool> active(rules.size(), false);
        for (std::size_t index = 0; index < rules.size(); ++index) {
            const auto& rule = rules[index];
            const bool listed = std::ranges::any_of(rule.process_names, [&](const auto& name) {
                    return rule.exact_match ? exact && process == name
                        : !process.empty() && process_stem(process).starts_with(process_stem(name));
                });
            active[index] = rule.enabled && (mapping_application_scope(rule) == ApplicationScope::all
                || (rule.application_scope == ApplicationScope::excluded ? !listed : listed));
        }
        std::vector<int> visit(rules.size(), 0);
        const auto visit_rule = [&](const auto& self, const std::size_t index) -> bool {
            if (!active[index]) return false;
            if (visit[index] == 1) return true;
            if (visit[index] == 2) return false;
            visit[index] = 1;
            for (const auto next : edges[index]) {
                if (self(self, next)) return true;
            }
            visit[index] = 2;
            return false;
        };
        for (std::size_t index = 0; index < rules.size(); ++index) {
            if (visit_rule(visit_rule, index)) {
                errors.push_back({index, L"Mapping cycle is not allowed.",
                    KeyboardMappingValidationKind::cycle, process});
                return errors;
            }
        }
    }
    return errors;
}

} // namespace simpilot
