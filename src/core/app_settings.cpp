#include "simpilot/app_settings.hpp"
#include "simpilot/text_encoding.hpp"
#include "simpilot/settings_document.hpp"
#include "simpilot/hotkey_settings_codec.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <sstream>
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

} // namespace

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
        if (!decoded) {
            if (diagnostic_sink) diagnostic_sink(L"settings document is not valid UTF-8");
            return AppSettings{};
        }
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
        load_binding(values, L"OpenSettings", result.open_settings.binding);
        result.open_settings.enabled = result.open_settings.binding.gesture
            && boolean_value(values, L"opensettingsenabled", true);
    } catch (...) {
        return AppSettings{};
    }
    return result;
}

void AppSettingsStore::write(SettingsDocument& document, const AppSettings& settings) {
    const auto language = settings.language == UiLanguage::external
        ? settings.language_code : std::string(Localization::language_code(settings.language));
    document.set(L"General", L"Language", std::wstring(language.begin(), language.end()));
    document.set(L"General", L"StartWithWindows", settings.start_with_windows ? L"1" : L"0");
    write_hotkey_setting(document, L"OpenSettings", settings.open_settings);
}

bool AppSettingsStore::save(const std::filesystem::path& path,
    const AppSettings& settings, const SettingsDocument* base) noexcept {
    try {
        auto document = base ? *base : SettingsDocument::load(path);
        write(document, settings);
        return document.save(path);
    } catch (...) { return false; }
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
