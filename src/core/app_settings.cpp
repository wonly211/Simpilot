#include "simpilot/app_settings.hpp"
#include "simpilot/atomic_file.hpp"
#include "simpilot/text_encoding.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
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
    const auto found = values.find(key);
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

UiLanguage language_value(
    const std::unordered_map<std::wstring, std::wstring>& values,
    std::string& external_code) noexcept {
    const auto raw_value = trim(string_value(values, L"Language"));
    const auto value = lowercase(raw_value);
    if (value == L"en-us") return UiLanguage::english;
    if (value == L"zh-tw") return UiLanguage::traditional_chinese;
    if (value == L"zh-cn" || value.empty()) return UiLanguage::simplified_chinese;
    const auto separator = raw_value.find(L'-');
    const auto locale_shape = separator == 2 && raw_value.size() == 5
        && iswalpha(raw_value[0]) && iswalpha(raw_value[1])
        && iswalpha(raw_value[3]) && iswalpha(raw_value[4]);
    if (!locale_shape) return UiLanguage::simplified_chinese;
    external_code = encode_utf8(raw_value);
    return external_code.empty() ? UiLanguage::simplified_chinese : UiLanguage::external;
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

void load_binding(const std::unordered_map<std::wstring, std::wstring>& values,
                  const std::wstring& key, HotKeyBinding& binding) {
    const auto normalized_key = lowercase(key);
    const auto code = values.find(normalized_key + L"code");
    if (code == values.end()) return;
    binding = {};
    const auto separator = code->second.find(L',');
    try {
        if (separator != std::wstring::npos) {
            const auto modifiers = static_cast<UINT>(
                std::stoul(trim(code->second.substr(0, separator))));
            const auto virtual_key = static_cast<UINT>(
                std::stoul(trim(code->second.substr(separator + 1))));
            if (virtual_key > 0 && virtual_key <= 0xFF) {
                binding.gesture = HotKeyGesture{
                    modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN),
                    virtual_key};
            }
        }
    } catch (...) {
    }
    binding.force_override = boolean_value(values, normalized_key + L"force", false)
        && binding.gesture.has_value();
    if (binding.gesture) {
        const auto index = windows_letter_hotkey_index(*binding.gesture);
        if (index && *index == static_cast<std::size_t>(L'L' - L'A')) binding = {};
    }
}

void write_binding(std::ofstream& stream, const char* key, const HotKeyBinding& binding) {
    stream << key << "Code=";
    if (binding.gesture) {
        stream << binding.gesture->modifiers << ',' << binding.gesture->virtual_key;
    }
    stream << "\r\n" << key << "Force=" << (binding.force_override ? 1 : 0) << "\r\n";
}

void load_disabled_windows_hotkeys(
    const std::unordered_map<std::wstring, std::wstring>& values,
    std::array<bool, 26>& state) noexcept {
    const auto found = values.find(L"disabledwindowshotkeys");
    if (found == values.end()) return;
    for (auto character : found->second) {
        if (character >= L'a' && character <= L'z') character -= L'a' - L'A';
        if (character >= L'A' && character <= L'Z') {
            const auto index = static_cast<std::size_t>(character - L'A');
            if (index != static_cast<std::size_t>(L'L' - L'A')) state[index] = true;
        }
    }
}

void write_disabled_windows_hotkeys(
    std::ofstream& stream, const std::array<bool, 26>& state) {
    stream << "\r\n[WindowsHotkeys]\r\nDisabledWindowsHotkeys=";
    for (std::size_t index = 0; index < state.size(); ++index) {
        if (index != static_cast<std::size_t>(L'L' - L'A') && state[index]) {
            stream << static_cast<char>('A' + index);
        }
    }
    stream << "\r\n";
}

void load_custom_global_hotkeys(
    const std::unordered_map<std::wstring, std::wstring>& values,
    std::vector<CustomGlobalHotKey>& hotkeys) {
    constexpr unsigned int maximum_custom_hotkeys = 128;
    const auto count = unsigned_value(
        values, L"CustomGlobalHotKeyCount", 0, maximum_custom_hotkeys);
    hotkeys.reserve(count);
    for (unsigned int number = 1; number <= count; ++number) {
        const auto prefix = L"CustomGlobalHotKey" + std::to_wstring(number);
        CustomGlobalHotKey hotkey;
        load_binding(values, prefix, hotkey.binding);
        hotkey.action = static_cast<CustomHotKeyAction>(
            unsigned_value(values, prefix + L"Action", 0, 2));
        hotkey.program_path = string_value(values, prefix + L"Program");
        hotkey.arguments = string_value(values, prefix + L"Arguments");
        hotkey.working_directory = string_value(values, prefix + L"WorkingDirectory");
        hotkey.run_as_administrator = boolean_value(
            values, lowercase(prefix + L"RunAsAdministrator"), false);
        hotkey.existing_process_action = static_cast<ExistingProcessAction>(
            unsigned_value(values, prefix + L"ExistingProcessAction", 0, 2));
        hotkey.visibility = static_cast<LaunchVisibility>(
            unsigned_value(values, prefix + L"Visibility", 0, 3));
        const auto enabled_key = lowercase(prefix + L"Enabled");
        if (!values.contains(enabled_key)) continue;
        hotkey.enabled = boolean_value(values, enabled_key, false);
        if (hotkey.binding.gesture && !hotkey.program_path.empty()) {
            if (const auto index = windows_letter_hotkey_index(*hotkey.binding.gesture);
                index && *index == static_cast<std::size_t>(L'L' - L'A')) {
                continue;
            }
            if (is_supported_windows_letter_hotkey(*hotkey.binding.gesture)) {
                hotkey.binding.force_override = true;
            }
            hotkeys.push_back(std::move(hotkey));
        }
    }
}

void write_custom_global_hotkeys(
    std::ofstream& stream, const std::vector<CustomGlobalHotKey>& hotkeys) {
    stream << "\r\n[CustomGlobalHotkeys]\r\nCustomGlobalHotKeyCount="
           << hotkeys.size() << "\r\n";
    for (std::size_t index = 0; index < hotkeys.size(); ++index) {
        const auto prefix = "CustomGlobalHotKey" + std::to_string(index + 1);
        const auto& hotkey = hotkeys[index];
        write_binding(stream, prefix.c_str(), hotkey.binding);
        stream << prefix << "Action=" << static_cast<unsigned int>(hotkey.action) << "\r\n"
               << prefix << "Program=" << encode_utf8(hotkey.program_path) << "\r\n"
               << prefix << "Arguments=" << encode_utf8(hotkey.arguments) << "\r\n"
               << prefix << "WorkingDirectory=" << encode_utf8(hotkey.working_directory) << "\r\n"
               << prefix << "RunAsAdministrator="
               << (hotkey.run_as_administrator ? 1 : 0) << "\r\n"
               << prefix << "ExistingProcessAction="
               << static_cast<unsigned int>(hotkey.existing_process_action) << "\r\n"
               << prefix << "Visibility="
               << static_cast<unsigned int>(hotkey.visibility) << "\r\n"
               << prefix << "Enabled=" << (hotkey.enabled ? 1 : 0) << "\r\n";
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
    const AppSettingsStore::DiagnosticSink& diagnostic_sink) {
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
        const auto process_value = string_value(values, prefix + L"Process");
        const auto process_name = normalize_mapping_process_name(process_value);
        if (!trim(process_value).empty() && process_name.empty()) {
            diagnose(number, L"invalid process scope");
            continue;
        }
        KeyboardMappingRule rule;
        rule.purpose = string_value(values, prefix + L"Purpose");
        rule.enabled = boolean_value(values, enabled_key, false);
        rule.process_name = process_name;
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
    std::ofstream& stream, const bool enabled,
    const std::vector<KeyboardMappingRule>& mappings) {
    stream << "\r\n[KeyboardMappings]\r\nKeyboardMappingsEnabled="
           << (enabled ? 1 : 0) << "\r\nKeyboardMappingCount="
           << std::min<std::size_t>(mappings.size(), 128) << "\r\n";
    const auto count = std::min<std::size_t>(mappings.size(), 128);
    for (std::size_t index = 0; index < count; ++index) {
        const auto prefix = "KeyboardMapping" + std::to_string(index + 1);
        const auto& mapping = mappings[index];
        stream << prefix << "Enabled=" << (mapping.enabled ? 1 : 0) << "\r\n"
               << prefix << "Purpose=" << encode_utf8(mapping.purpose) << "\r\n"
               << prefix << "Process=" << encode_utf8(mapping.process_name) << "\r\n"
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

bool global_hotkey_requires_windows_blocking(
    const AppSettings& settings, const std::size_t windows_letter_index) noexcept {
    if (windows_letter_index >= settings.disabled_windows_hotkeys.size()
        || windows_letter_index == static_cast<std::size_t>(L'L' - L'A')) {
        return false;
    }
    const std::array built_in_hotkeys{
        &settings.main_menu, &settings.second_menu,
        &settings.open_settings, &settings.everything_search,
    };
    for (const auto* hotkey : built_in_hotkeys) {
        if (hotkey->enabled && hotkey->binding.gesture
            && windows_letter_hotkey_index(*hotkey->binding.gesture)
                == windows_letter_index) {
            return true;
        }
    }
    return std::ranges::any_of(settings.custom_global_hotkeys,
        [windows_letter_index](const CustomGlobalHotKey& hotkey) {
            return hotkey.enabled && hotkey.binding.gesture
                && windows_letter_hotkey_index(*hotkey.binding.gesture)
                    == windows_letter_index;
        });
}

AppSettings AppSettingsStore::load(
    const std::filesystem::path& path,
    DiagnosticSink diagnostic_sink) noexcept {
    AppSettings result;
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return result;
        const std::string content((std::istreambuf_iterator<char>(stream)),
                                  std::istreambuf_iterator<char>());
        auto decoded = decode_utf8(content.starts_with("\xEF\xBB\xBF")
            ? std::string_view(content).substr(3) : std::string_view(content));
        if (!decoded) return AppSettings{};
        auto text = std::move(*decoded);
        std::unordered_map<std::wstring, std::wstring> values;
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find_first_of(L"\r\n", start);
            auto line = trim(text.substr(start, end - start));
            if (!line.empty() && line.front() != L';' && line.front() != L'#'
                && line.front() != L'[') {
                const auto separator = line.find(L'=');
                if (separator != std::wstring::npos) {
                    values.insert_or_assign(lowercase(trim(line.substr(0, separator))),
                                            trim(line.substr(separator + 1)));
                }
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
            if (text[end] == L'\r' && start < text.size() && text[start] == L'\n') ++start;
        }
        result.language = language_value(values, result.language_code);
        result.start_with_windows = boolean_value(values, L"startwithwindows", false);
        result.mouse_shake_locator_enabled = boolean_value(
            values, L"mouseshakelocatorenabled", false);
        result.menu_theme = static_cast<MenuTheme>(
            unsigned_value(values, L"MenuTheme", 0, 2));
        load_binding(values, L"MainMenu", result.main_menu.binding);
        load_binding(values, L"SecondMenu", result.second_menu.binding);
        load_binding(values, L"OpenSettings", result.open_settings.binding);
        result.main_menu.enabled = result.main_menu.binding.gesture
            && boolean_value(values, L"mainmenuenabled", true);
        result.second_menu.enabled = result.second_menu.binding.gesture
            && boolean_value(values, L"secondmenuenabled", true);
        result.open_settings.enabled = result.open_settings.binding.gesture
            && boolean_value(values, L"opensettingsenabled", true);
        load_binding(values, L"EverythingSearch", result.everything_search.binding);
        result.everything_search.enabled = boolean_value(
            values, L"everythingsearchenabled", false);
        if (result.everything_search.binding.gesture
            && is_supported_windows_letter_hotkey(
                *result.everything_search.binding.gesture)) {
            result.everything_search.binding.force_override = true;
        }
        load_disabled_windows_hotkeys(values, result.disabled_windows_hotkeys);
        load_custom_global_hotkeys(values, result.custom_global_hotkeys);
        load_keyboard_mappings(values, result.keyboard_mappings_enabled,
                               result.keyboard_mappings, diagnostic_sink);
    } catch (...) {
        return AppSettings{};
    }
    return result;
}

bool AppSettingsStore::save(const std::filesystem::path& path,
                             const AppSettings& settings) noexcept {
    try {
        auto mappings = settings.keyboard_mappings;
        for (auto& mapping : mappings) {
            if (!mapping.process_name.empty()) {
                const auto normalized = normalize_mapping_process_name(
                    mapping.process_name);
                if (normalized.empty()) return false;
                mapping.process_name = normalized;
            }
        }
        if (!validate_keyboard_mappings(mappings).empty()) return false;
        AtomicFileReplacement replacement(path);
        {
            std::ofstream stream(
                replacement.temporary_path(), std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            const auto language = settings.language == UiLanguage::external
                ? settings.language_code
                : std::string(Localization::language_code(settings.language));
            stream << "[General]\r\nLanguage=" << language
                   << "\r\nStartWithWindows=" << (settings.start_with_windows ? 1 : 0)
                   << "\r\nMouseShakeLocatorEnabled="
                   << (settings.mouse_shake_locator_enabled ? 1 : 0)
                   << "\r\nMenuTheme=" << static_cast<unsigned int>(settings.menu_theme)
                   << "\r\n\r\n[Hotkeys]\r\n";
            write_binding(stream, "MainMenu", settings.main_menu.binding);
            stream << "MainMenuEnabled=" << (settings.main_menu.enabled ? 1 : 0) << "\r\n";
            write_binding(stream, "SecondMenu", settings.second_menu.binding);
            stream << "SecondMenuEnabled=" << (settings.second_menu.enabled ? 1 : 0) << "\r\n";
            write_binding(stream, "OpenSettings", settings.open_settings.binding);
            stream << "OpenSettingsEnabled=" << (settings.open_settings.enabled ? 1 : 0) << "\r\n";
            write_binding(stream, "EverythingSearch", settings.everything_search.binding);
            stream << "EverythingSearchEnabled="
                   << (settings.everything_search.enabled ? 1 : 0) << "\r\n";
            write_disabled_windows_hotkeys(stream, settings.disabled_windows_hotkeys);
            write_custom_global_hotkeys(stream, settings.custom_global_hotkeys);
            write_keyboard_mappings(stream, settings.keyboard_mappings_enabled,
                                    mappings);
            if (!stream) return false;
        }
        return replacement.commit();
    } catch (...) {
        return false;
    }
}

bool StartupRegistration::apply(const bool enabled,
                                const std::filesystem::path& executable_path) noexcept {
    constexpr auto key_path = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr auto value_name = L"Simpilot";
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key_path, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LONG result = ERROR_SUCCESS;
    if (!enabled) {
        result = RegDeleteValueW(key, value_name);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    } else {
        const auto command = L"\"" + std::filesystem::absolute(executable_path).wstring() + L"\"";
        result = RegSetValueExW(key, value_name, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

} // namespace simpilot
