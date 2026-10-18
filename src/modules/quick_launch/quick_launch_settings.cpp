#include "quick_launch_settings.hpp"
#include "simpilot/hotkey_settings_codec.hpp"
namespace simpilot {
QuickLaunchSettings QuickLaunchSettings::read(const SettingsDocument& document) {
    QuickLaunchSettings result;
    const auto size = document.get(L"MenuSize").value_or(L"1");
    if (size == L"0") result.menu_size = MenuSize::Small;
    if (size == L"2") result.menu_size = MenuSize::Large;
    result.main_menu = read_hotkey_setting(document, L"MainMenu", result.main_menu);
    result.second_menu = read_hotkey_setting(document, L"SecondMenu", result.second_menu);
    try {
        result.menu_theme = static_cast<MenuTheme>(
            std::min(std::stoul(document.get(L"MenuTheme").value_or(L"0")), 2ul));
    } catch (...) {}
    return result;
}
void QuickLaunchSettings::write(SettingsDocument& document) const {
    document.set(L"General", L"MenuSize", std::to_wstring(static_cast<int>(menu_size)));
    write_hotkey_setting(document, L"MainMenu", main_menu);
    write_hotkey_setting(document, L"SecondMenu", second_menu);
    document.set(L"General", L"MenuTheme", std::to_wstring(static_cast<int>(menu_theme)));
}
} // namespace simpilot
