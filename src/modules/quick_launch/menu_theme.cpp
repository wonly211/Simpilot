#include "menu_theme.hpp"

#include <Windows.h>

namespace simpilot {
namespace {

enum class PreferredAppMode {
    default_mode,
    allow_dark,
    force_dark,
    force_light,
};

using SetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);
using FlushMenuThemes = void(WINAPI*)();
using RtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

bool supports_preferred_app_mode() noexcept {
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto rtl_get_version = module
        ? reinterpret_cast<RtlGetVersion>(GetProcAddress(module, "RtlGetVersion")) : nullptr;
    RTL_OSVERSIONINFOW version{.dwOSVersionInfoSize = sizeof(version)};
    return rtl_get_version && rtl_get_version(&version) == 0
        && version.dwMajorVersion >= 10 && version.dwBuildNumber >= 18362;
}

} // namespace

bool MenuThemeController::apply(const MenuTheme theme) noexcept {
    if (!supports_preferred_app_mode()) return false;
    const auto module = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return false;
    const auto set_mode = reinterpret_cast<SetPreferredAppMode>(
        GetProcAddress(module, MAKEINTRESOURCEA(135)));
    const auto flush = reinterpret_cast<FlushMenuThemes>(
        GetProcAddress(module, MAKEINTRESOURCEA(136)));
    if (!set_mode || !flush) {
        FreeLibrary(module);
        return false;
    }
    const auto mode = theme == MenuTheme::dark ? PreferredAppMode::force_dark
        : theme == MenuTheme::light ? PreferredAppMode::force_light
                                  : PreferredAppMode::allow_dark;
    (void)set_mode(mode);
    flush();
    FreeLibrary(module);
    return true;
}

bool MenuThemeController::dark_mode_enabled(const MenuTheme theme) noexcept {
    if (theme == MenuTheme::dark) return true;
    if (theme == MenuTheme::light) return false;
    DWORD light_theme = 1;
    DWORD size = sizeof(light_theme);
    const auto result = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light_theme, &size);
    return result == ERROR_SUCCESS && light_theme == 0;
}

} // namespace simpilot
