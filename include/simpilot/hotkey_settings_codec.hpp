#pragma once
#include "simpilot/hotkey.hpp"
#include "simpilot/settings_document.hpp"
#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace simpilot {
// Import validation is strict; the regular reader remains compatible with old files.
inline void validate_hotkey_setting(const SettingsDocument& document, const std::wstring& prefix) {
    const auto value = document.get(prefix + L"Code");
    if (!value || value->empty()) return; // An explicitly cleared binding is valid.
    const auto comma = value->find(L',');
    const auto number = [](std::wstring text) {
        const auto first = text.find_first_not_of(L" \t");
        if (first == text.npos) throw std::runtime_error("Invalid hotkey code");
        text = text.substr(first, text.find_last_not_of(L" \t") - first + 1);
        if (text.find_first_not_of(L"0123456789") != text.npos) throw std::runtime_error("Invalid hotkey code");
        return std::stoul(text);
    };
    if (comma == value->npos) throw std::runtime_error("Invalid hotkey code");
    const auto modifiers = number(value->substr(0, comma));
    const auto key = number(value->substr(comma + 1));
    if (modifiers > (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN) || !key || key > 0xff
        || ((modifiers & MOD_WIN) && key == L'L')) throw std::runtime_error("Unsupported hotkey code");
}
inline bool settings_boolean(const SettingsDocument& document, const std::wstring& key, bool fallback) {
    auto value = document.get(key);
    if (!value) return fallback;
    std::ranges::transform(*value, value->begin(), [](wchar_t c) { return std::towlower(c); });
    return *value == L"1" || *value == L"true" || *value == L"yes" || *value == L"on";
}
inline BuiltInHotKey read_hotkey_setting(const SettingsDocument& document,
    const std::wstring& prefix, BuiltInHotKey fallback = {}) {
    if (const auto value = document.get(prefix + L"Code")) {
        fallback.binding = {};
        const auto comma = value->find(L',');
        if (comma != std::wstring::npos) {
            try {
                const auto modifiers = static_cast<UINT>(std::stoul(value->substr(0, comma)));
                const auto key = static_cast<UINT>(std::stoul(value->substr(comma + 1)));
                if (key > 0 && key <= 0xff) fallback.binding.gesture = HotKeyGesture{
                    modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN), key};
            } catch (...) {}
        }
        fallback.binding.force_override = fallback.binding.gesture
            && settings_boolean(document, prefix + L"Force", false);
    }
    if (fallback.binding.gesture
        && windows_letter_hotkey_index(*fallback.binding.gesture) == static_cast<std::size_t>(L'L' - L'A')) {
        fallback.binding = {};
    }
    fallback.enabled = fallback.binding.gesture && settings_boolean(document, prefix + L"Enabled", true);
    return fallback;
}
inline void write_hotkey_setting(SettingsDocument& document, const std::wstring& prefix,
                                const BuiltInHotKey& value) {
    document.set(L"Hotkeys", prefix + L"Code", value.binding.gesture
        ? std::to_wstring(value.binding.gesture->modifiers) + L","
            + std::to_wstring(value.binding.gesture->virtual_key) : L"");
    document.set(L"Hotkeys", prefix + L"Force", value.binding.force_override ? L"1" : L"0");
    document.set(L"Hotkeys", prefix + L"Enabled", value.enabled ? L"1" : L"0");
}
} // namespace simpilot
