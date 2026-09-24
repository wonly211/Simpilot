#pragma once
#include "simpilot/hotkey.hpp"
#include "simpilot/settings_document.hpp"
#include <algorithm>
#include <cwctype>

namespace simpilot {
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
