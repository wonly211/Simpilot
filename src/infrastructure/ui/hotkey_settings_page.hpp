#pragma once

#include "simpilot/hotkey_registry.hpp"

namespace simpilot {
class KeyboardManager;

std::unique_ptr<ISettingsPage> make_hotkey_settings_page(
    HotkeyRegistry& hotkeys, KeyboardManager& keyboard,
    std::function<bool(const HotKeyGesture&)> availability,
    std::function<void(std::wstring_view)> diagnose);

// Shared conflict/availability prompts used by built-in and contributed editors.
bool confirm_hotkey_availability(HWND owner, const Localization& localization,
    BuiltInHotKey& value, const std::function<bool(const HotKeyGesture&)>& available);
bool confirm_hotkey_replacement(HWND owner, const Localization& localization,
    const HotKeyGesture& gesture, const HotkeyDraftBinding& conflict);
} // namespace simpilot
