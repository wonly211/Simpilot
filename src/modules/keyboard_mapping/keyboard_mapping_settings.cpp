#include "keyboard_mapping_module.hpp"
#include "simpilot/text_encoding.hpp"
#include <algorithm>
#include <cwctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

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

bool boolean_value(const std::unordered_map<std::wstring, std::wstring>& values,
                   const std::wstring& key, const bool fallback) {
    const auto found = values.find(lowercase(key));
    if (found == values.end()) return fallback;
    const auto value = lowercase(trim(found->second));
    return value == L"1" || value == L"true" || value == L"yes" || value == L"on";
}

std::wstring string_value(
    const std::unordered_map<std::wstring, std::wstring>& values,
    const std::wstring& key) {
    const auto found = values.find(lowercase(key));
    return found == values.end() ? std::wstring{} : found->second;
}

unsigned int unsigned_value(
    const std::unordered_map<std::wstring, std::wstring>& values,
    const std::wstring& key, const unsigned int fallback,
    const unsigned int maximum) noexcept {
    const auto found = values.find(lowercase(key));
    if (found == values.end()) return fallback;
    try {
        return std::min(static_cast<unsigned int>(std::stoul(trim(found->second))), maximum);
    } catch (...) {
        return fallback;
    }
}

std::optional<UINT> parse_mapping_number(const std::wstring_view value) {
    const auto normalized = trim(std::wstring(value));
    if (normalized.empty()
        || normalized.find_first_not_of(L"0123456789") != std::wstring::npos) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoul(normalized, &consumed, 10);
        if (consumed != normalized.size()
            || parsed > std::numeric_limits<UINT>::max()) {
            return std::nullopt;
        }
        return static_cast<UINT>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<PhysicalKey> parse_mapping_key(const std::wstring& value) {
    const auto first = value.find(L':');
    const auto second = first == std::wstring::npos
        ? std::wstring::npos : value.find(L':', first + 1);
    if (first == std::wstring::npos || second == std::wstring::npos
        || value.find(L':', second + 1) != std::wstring::npos) {
        return std::nullopt;
    }
    const auto virtual_key = parse_mapping_number(value.substr(0, first));
    const auto scan_code = parse_mapping_number(
        value.substr(first + 1, second - first - 1));
    const auto extended = parse_mapping_number(value.substr(second + 1));
    if (!virtual_key || !scan_code || !extended || *extended > 1) {
        return std::nullopt;
    }
    const PhysicalKey key{*virtual_key, *scan_code, *extended != 0};
    return is_mapping_key_valid(key) ? std::optional<PhysicalKey>(key)
                                     : std::nullopt;
}

std::optional<std::vector<PhysicalKey>> parse_mapping_modifiers(
    const std::wstring& value) {
    std::vector<PhysicalKey> result;
    if (trim(value).empty()) return result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(L'|', start);
        const auto token = trim(value.substr(start, end - start));
        const auto key = parse_mapping_key(token);
        if (!key || !is_mapping_modifier(key->virtual_key) || result.size() >= 4) {
            return std::nullopt;
        }
        result.push_back(*key);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return result;
}

std::wstring mapping_key_text(const PhysicalKey& key) {
    return std::to_wstring(key.virtual_key) + L":"
        + std::to_wstring(key.scan_code) + L":"
        + (key.extended ? L"1" : L"0");
}

std::wstring mapping_modifier_text(
    const std::array<PhysicalKey, 4>& modifiers, const std::size_t count) {
    std::wstring result;
    for (std::size_t index = 0; index < count; ++index) {
        if (!result.empty()) result.push_back(L'|');
        result.append(mapping_key_text(modifiers[index]));
    }
    return result;
}

void load_keyboard_mappings(
    const std::unordered_map<std::wstring, std::wstring>& values,
    bool& enabled, std::vector<KeyboardMappingRule>& mappings,
    const std::function<void(std::wstring_view)>& diagnostic_sink) {
    constexpr unsigned int maximum_mappings = 128;
    const auto diagnose = [&diagnostic_sink](const unsigned int number,
                                             const std::wstring_view reason) noexcept {
        if (!diagnostic_sink) return;
        try {
            diagnostic_sink(L"keyboard mapping " + std::to_wstring(number)
                + L" skipped: " + std::wstring(reason));
        } catch (...) {
        }
    };
    enabled = boolean_value(values, L"KeyboardMappingsEnabled", true);
    const auto count = unsigned_value(
        values, L"KeyboardMappingCount", 0, maximum_mappings);
    mappings.reserve(count);
    for (unsigned int number = 1; number <= count; ++number) {
        const auto prefix = L"KeyboardMapping" + std::to_wstring(number);
        const auto enabled_key = lowercase(prefix + L"Enabled");
        if (!values.contains(enabled_key)) {
            diagnose(number, L"missing Enabled field");
            continue;
        }
        const auto modifiers = parse_mapping_modifiers(
            string_value(values, prefix + L"SourceModifiers"));
        const auto target_modifiers = parse_mapping_modifiers(
            string_value(values, prefix + L"TargetModifiers"));
        const auto source_action = parse_mapping_key(
            string_value(values, prefix + L"SourceAction"));
        const auto source_chord_value = string_value(
            values, prefix + L"SourceChord");
        const auto source_chord = source_chord_value.empty()
            || trim(source_chord_value) == L"0"
            ? std::optional<PhysicalKey>{}
            : parse_mapping_key(source_chord_value);
        const auto target_action = parse_mapping_key(
            string_value(values, prefix + L"TargetAction"));
        if (!modifiers || !target_modifiers || !source_action ||
            (!trim(source_chord_value).empty() && trim(source_chord_value) != L"0"
             && !source_chord) || !target_action) {
            diagnose(number, L"invalid persisted key fields");
            continue;
        }
        KeyboardMappingRule rule;
        rule.purpose = string_value(values, prefix + L"Purpose");
        rule.enabled = boolean_value(values, enabled_key, false);
        const auto process_value = string_value(values, prefix + L"Process");
        const auto scope_value = string_value(values, prefix + L"Scope");
        const bool excluded = scope_value == L"exclude";
        if ((!scope_value.empty() && !excluded)
            || (excluded && !values.contains(lowercase(prefix + L"ProcessCount")))) {
            diagnose(number, L"invalid application scope");
            continue;
        }
        if (excluded) rule.application_scope = ApplicationScope::excluded;
        if (values.contains(lowercase(prefix + L"ProcessCount"))) {
            const auto process_count = parse_mapping_number(
                string_value(values, prefix + L"ProcessCount"));
            if (!process_count || *process_count < (excluded ? 1u : 2u) || *process_count > maximum_mapping_applications
                || process_value != (excluded ? L":exclude-app:" : L":multi-app:")) {
                diagnose(number, L"invalid application list count");
                continue;
            }
            for (UINT process = 1; process <= *process_count; ++process) {
                rule.process_names.push_back(string_value(
                    values, prefix + L"Process" + std::to_wstring(process)));
            }
        } else if (!trim(process_value).empty()) {
            // Legacy single-app/global settings remain readable. In particular,
            // never treat an invalid downgrade guard as an empty/global scope.
            rule.process_names.push_back(process_value);
        }
        auto names = normalize_mapping_process_names(rule.process_names);
        if (!names) {
            diagnose(number, L"invalid process scope");
            continue;
        }
        rule.process_names = std::move(*names);
        rule.exact_match = boolean_value(
            values, prefix + L"ExactMatch", true);
        rule.trigger.single_key = modifiers->empty() && !source_chord;
        rule.trigger.modifier_count = modifiers->size();
        std::ranges::copy(*modifiers, rule.trigger.modifiers.begin());
        rule.trigger.action = *source_action;
        rule.trigger.chord_action = source_chord;
        rule.output.single_key = target_modifiers->empty();
        rule.output.modifier_count = target_modifiers->size();
        std::ranges::copy(*target_modifiers, rule.output.modifiers.begin());
        rule.output.action = *target_action;
        auto accepted = mappings;
        accepted.push_back(rule);
        const auto errors = validate_keyboard_mappings(accepted);
        if (!errors.empty()) {
            diagnose(number, errors.front().message);
            continue;
        }
        mappings.push_back(std::move(rule));
    }
}

void write_keyboard_mappings(
    std::ostream& stream, const bool enabled,
    const std::vector<KeyboardMappingRule>& mappings) {
    stream << "\r\n[KeyboardMappings]\r\nKeyboardMappingsEnabled="
           << (enabled ? 1 : 0) << "\r\nKeyboardMappingCount="
           << std::min<std::size_t>(mappings.size(), 128) << "\r\n";
    const auto count = std::min<std::size_t>(mappings.size(), 128);
    for (std::size_t index = 0; index < count; ++index) {
        const auto prefix = "KeyboardMapping" + std::to_string(index + 1);
        const auto& mapping = mappings[index];
        // v1.0.9 and earlier reject ':' in Process and skip this rule. Omitting
        // Process instead would dangerously turn a multi-app rule into global.
        const bool excluded = mapping.application_scope == ApplicationScope::excluded;
        const auto legacy_process = excluded ? std::wstring(L":exclude-app:") : mapping.process_names.size() > 1
            ? std::wstring(L":multi-app:") : mapping.process_names.empty()
            ? std::wstring{} : mapping.process_names.front();
        // Keep single-app/global rules entirely in the legacy format, so edits
        // in an older version cannot leave a stale parallel list behind.
        if (excluded) stream << prefix << "Scope=exclude\r\n";
        if (excluded || mapping.process_names.size() > 1) {
            stream << prefix << "ProcessCount=" << mapping.process_names.size() << "\r\n";
            for (std::size_t process = 0; process < mapping.process_names.size(); ++process) {
                stream << prefix << "Process" << process + 1 << "="
                       << encode_utf8(mapping.process_names[process]) << "\r\n";
            }
        }
        stream << prefix << "Enabled=" << (mapping.enabled ? 1 : 0) << "\r\n"
               << prefix << "Purpose=" << encode_utf8(mapping.purpose) << "\r\n"
               << prefix << "Process=" << encode_utf8(legacy_process) << "\r\n"
               << prefix << "ExactMatch=" << (mapping.exact_match ? 1 : 0) << "\r\n"
               << prefix << "SourceModifiers="
               << encode_utf8(mapping_modifier_text(
                   mapping.trigger.modifiers, mapping.trigger.modifier_count)) << "\r\n"
               << prefix << "SourceAction=" << encode_utf8(
                   mapping_key_text(mapping.trigger.action)) << "\r\n"
               << prefix << "SourceChord=";
        if (mapping.trigger.chord_action) {
            stream << encode_utf8(mapping_key_text(*mapping.trigger.chord_action));
        } else {
            stream << "0";
        }
        stream << "\r\n" << prefix << "TargetModifiers="
               << encode_utf8(mapping_modifier_text(
                   mapping.output.modifiers, mapping.output.modifier_count)) << "\r\n"
               << prefix << "TargetAction=" << encode_utf8(
                   mapping_key_text(mapping.output.action)) << "\r\n";
    }
}

} // namespace

void inspect_keyboard_mapping_backup(const SettingsSnapshot& snapshot) {
    const auto document = SettingsDocument::parse(snapshot.text("Config/Setting.ini"));
    const auto count = parse_mapping_number(document.get(L"KeyboardMappingCount").value_or(L"0"));
    bool invalid = false;
    const auto settings = KeyboardMappingSettings::read(document, [&](std::wstring_view) { invalid = true; });
    if (!count || *count > maximum_keyboard_mappings || invalid || settings.rules.size() != *count)
        throw std::runtime_error("Invalid keyboard mapping rules");
}

KeyboardMappingSettings KeyboardMappingSettings::read(
    const SettingsDocument& document, std::function<void(std::wstring_view)> diagnose) {
    std::unordered_map<std::wstring, std::wstring> values;
    const auto collect = [&](const std::wstring& key) {
        if (auto value = document.get(key)) values.emplace(lowercase(key), std::move(*value));
    };
    collect(L"KeyboardMappingsEnabled");
    collect(L"KeyboardMappingCount");
    constexpr std::wstring_view suffixes[]{
        L"Enabled", L"Purpose", L"Process", L"ProcessCount", L"Scope", L"ExactMatch", L"SourceModifiers",
        L"SourceAction", L"SourceChord", L"TargetModifiers", L"TargetAction"};
    const auto count = unsigned_value(values, L"KeyboardMappingCount", 0, 128);
    for (unsigned int number = 1; number <= count; ++number) {
        const auto prefix = L"KeyboardMapping" + std::to_wstring(number);
        for (const auto suffix : suffixes) {
            collect(prefix + std::wstring(suffix));
        }
        const auto process_count = parse_mapping_number(
            string_value(values, prefix + L"ProcessCount"));
        if (process_count && *process_count <= maximum_mapping_applications) {
            for (UINT process = 1; process <= *process_count; ++process) {
                collect(prefix + L"Process" + std::to_wstring(process));
            }
        }
    }
    KeyboardMappingSettings result;
    load_keyboard_mappings(values, result.enabled, result.rules, diagnose);
    return result;
}

void KeyboardMappingSettings::write(SettingsDocument& document) const {
    auto normalized = rules;
    for (auto& rule : normalized) {
        auto names = normalize_mapping_process_names(rule.process_names);
        if (!names) throw std::invalid_argument("Invalid mapping process scope");
        rule.process_names = std::move(*names);
    }
    if (!validate_keyboard_mappings(normalized).empty()) {
        throw std::invalid_argument("Invalid keyboard mappings");
    }
    std::ostringstream stream;
    write_keyboard_mappings(stream, enabled, normalized);
    constexpr std::wstring_view suffixes[]{
        L"Enabled", L"Purpose", L"Process", L"ProcessCount", L"Scope", L"ExactMatch", L"SourceModifiers",
        L"SourceAction", L"SourceChord", L"TargetModifiers", L"TargetAction"};
    document.erase_numbered(L"KeyboardMapping", suffixes);
    for (std::size_t process = 1; process <= maximum_mapping_applications; ++process) {
        const auto suffix = L"Process" + std::to_wstring(process);
        const std::wstring_view owned[]{suffix};
        document.erase_numbered(L"KeyboardMapping", owned);
    }
    document.overlay(SettingsDocument::parse(stream.str()));
}

} // namespace simpilot
