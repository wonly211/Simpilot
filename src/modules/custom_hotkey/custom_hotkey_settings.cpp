#include "custom_hotkey_settings.hpp"
#include <algorithm>
#include <cwctype>

namespace simpilot {
namespace {
unsigned number(const SettingsDocument& document, const std::wstring& key,
                unsigned fallback, unsigned maximum) {
    const auto value = document.get(key);
    if (!value) return fallback;
    try { return std::min(static_cast<unsigned>(std::stoul(*value)), maximum); }
    catch (...) { return fallback; }
}
bool boolean(const SettingsDocument& document, const std::wstring& key, bool fallback = false) {
    auto value = document.get(key);
    if (!value) return fallback;
    std::ranges::transform(*value, value->begin(), [](wchar_t c) { return std::towlower(c); });
    return *value == L"1" || *value == L"true" || *value == L"yes" || *value == L"on";
}
}
CustomHotkeySettings CustomHotkeySettings::read(const SettingsDocument& document) {
    CustomHotkeySettings result;
    const auto count = number(document, L"CustomGlobalHotKeyCount", 0, 128);
    result.items.reserve(count);
    for (unsigned i = 1; i <= count; ++i) {
        const auto prefix = L"CustomGlobalHotKey" + std::to_wstring(i);
        if (!document.get(prefix + L"Enabled")) continue;
        CustomGlobalHotKey item;
        const auto stored_code = document.get(prefix + L"Code");
        const auto code = stored_code.value_or(L"");
        const auto comma = code.find(L',');
        if (comma != std::wstring::npos) {
            try {
                const auto modifiers = static_cast<UINT>(std::stoul(code.substr(0, comma)));
                const auto key = static_cast<UINT>(std::stoul(code.substr(comma + 1)));
                if (key > 0 && key <= 0xff) {
                    item.binding.gesture = HotKeyGesture{
                        modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN), key};
                }
            } catch (...) {}
        }
        item.program_path = document.get(prefix + L"Program").value_or(L"");
        item.enabled = boolean(document, prefix + L"Enabled");
        const auto cleared_draft = stored_code && code.empty() && !item.enabled;
        if ((!item.binding.gesture && !cleared_draft) || item.program_path.empty()) continue;
        if (item.binding.gesture) {
            const auto letter = windows_letter_hotkey_index(*item.binding.gesture);
            if (letter == static_cast<std::size_t>(L'L' - L'A')) continue;
            item.binding.force_override = boolean(document, prefix + L"Force")
                || is_supported_windows_letter_hotkey(*item.binding.gesture);
        }
        item.action = static_cast<CustomHotKeyAction>(number(document, prefix + L"Action", 0, 2));
        item.arguments = document.get(prefix + L"Arguments").value_or(L"");
        item.working_directory = document.get(prefix + L"WorkingDirectory").value_or(L"");
        item.run_as_administrator = boolean(document, prefix + L"RunAsAdministrator");
        item.existing_process_action = static_cast<ExistingProcessAction>(
            number(document, prefix + L"ExistingProcessAction", 0, 2));
        item.visibility = static_cast<LaunchVisibility>(number(document, prefix + L"Visibility", 0, 3));
        result.items.push_back(std::move(item));
    }
    return result;
}
void CustomHotkeySettings::write(SettingsDocument& document) const {
    constexpr std::wstring_view suffixes[]{L"Code", L"Force", L"Action", L"Program",
        L"Arguments", L"WorkingDirectory", L"RunAsAdministrator", L"ExistingProcessAction",
        L"Visibility", L"Enabled"};
    document.erase_numbered(L"CustomGlobalHotKey", suffixes);
    document.set(L"CustomGlobalHotkeys", L"CustomGlobalHotKeyCount", std::to_wstring(items.size()));
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto prefix = L"CustomGlobalHotKey" + std::to_wstring(i + 1);
        const auto& item = items[i];
        const auto set = [&](std::wstring_view suffix, std::wstring_view value) {
            document.set(L"CustomGlobalHotkeys", prefix + std::wstring(suffix), value);
        };
        set(L"Code", item.binding.gesture
            ? std::to_wstring(item.binding.gesture->modifiers) + L","
                + std::to_wstring(item.binding.gesture->virtual_key) : L"");
        set(L"Force", item.binding.force_override ? L"1" : L"0");
        set(L"Action", std::to_wstring(static_cast<int>(item.action)));
        set(L"Program", item.program_path);
        set(L"Arguments", item.arguments);
        set(L"WorkingDirectory", item.working_directory);
        set(L"RunAsAdministrator", item.run_as_administrator ? L"1" : L"0");
        set(L"ExistingProcessAction", std::to_wstring(static_cast<int>(item.existing_process_action)));
        set(L"Visibility", std::to_wstring(static_cast<int>(item.visibility)));
        set(L"Enabled", item.enabled ? L"1" : L"0");
    }
}
} // namespace simpilot
