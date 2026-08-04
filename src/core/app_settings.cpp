#include "simpilot/app_settings.hpp"
#include "simpilot/text_encoding.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
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

AppSettings AppSettingsStore::load(const std::filesystem::path& path) noexcept {
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
    } catch (...) {
        return AppSettings{};
    }
    return result;
}

bool AppSettingsStore::save(const std::filesystem::path& path,
                            const AppSettings& settings) noexcept {
    try {
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = std::filesystem::path(path.wstring() + L".tmp");
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            const auto language = settings.language == UiLanguage::external
                ? settings.language_code
                : std::string(Localization::language_code(settings.language));
            stream << "[General]\r\nLanguage=" << language
                   << "\r\nStartWithWindows=" << (settings.start_with_windows ? 1 : 0)
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
            if (!stream) return false;
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
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
