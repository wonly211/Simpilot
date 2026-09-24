#pragma once
#include "simpilot/hotkey.hpp"
#include "simpilot/settings_document.hpp"

namespace simpilot {
enum class MenuTheme { system = 0, light = 1, dark = 2 };
struct QuickLaunchSettings {
    MenuTheme menu_theme = MenuTheme::system;
    BuiltInHotKey main_menu{{{HotKeyGesture{0, VK_OEM_3}}, false}, true};
    BuiltInHotKey second_menu{};
    static QuickLaunchSettings read(const SettingsDocument& document);
    void write(SettingsDocument& document) const;
    bool operator==(const QuickLaunchSettings&) const = default;
};
} // namespace simpilot
