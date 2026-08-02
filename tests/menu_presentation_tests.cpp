#include "launch_menu_renderer.hpp"
#include "menu_icon_cache.hpp"
#include "menu_theme.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path executable_path() {
    std::wstring value(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr, value.data(), static_cast<DWORD>(value.size()));
    require(length > 0 && length < value.size(), "Resolve test executable path");
    value.resize(length);
    return value;
}

std::filesystem::path system_directory() {
    std::wstring value(32768, L'\0');
    const auto length = GetSystemDirectoryW(value.data(), static_cast<UINT>(value.size()));
    require(length > 0 && length < value.size(), "Resolve the Windows system directory");
    value.resize(length);
    return value;
}

} // namespace

int wmain() {
    try {
        const auto root = std::filesystem::temp_directory_path()
            / (L"simpilot-menu-presentation-" + std::to_wstring(GetCurrentProcessId()));
        const auto cache_directory = root / L"Cache" / L"RunIcon";
        std::filesystem::remove_all(root);
        {
            simpilot::MenuIconCache icons(cache_directory);
            require(icons.folder_icon() != nullptr, "Load the Windows folder icon");

            simpilot::MenuEntry application{
                L"Simpilot test", executable_path().wstring(),
                simpilot::MenuEntryKind::command, 1};
            const auto application_icon = icons.icon_for(application);
            require(application_icon != nullptr,
                    "Load an application icon from its executable");

            const auto target = executable_path().wstring();
            const auto custom_key = simpilot::MenuIconCache::custom_key_for(application);
            require(icons.set_custom_icon(custom_key,
                                          system_directory() / L"shell32.dll", 0),
                    "Extract a custom icon from a DLL");
            require(icons.has_custom_icon(custom_key), "Persist the custom icon override");
            icons.clear();
            require(icons.icon_for_customization(custom_key, target,
                                                 simpilot::MenuEntryKind::command) != nullptr,
                    "Reload the custom icon override from disk");
            bool custom_icon_is_128 = false;
            for (const auto& entry : std::filesystem::directory_iterator(cache_directory)) {
                if (!entry.is_regular_file()
                    || entry.path().filename().wstring().find(L".custom.ico")
                        == std::wstring::npos) {
                    continue;
                }
                std::ifstream stream(entry.path(), std::ios::binary);
                std::vector<unsigned char> bytes(
                    (std::istreambuf_iterator<char>(stream)),
                    std::istreambuf_iterator<char>());
                custom_icon_is_128 = bytes.size() >= 22
                    && bytes[6] == 128 && bytes[7] == 128;
            }
            require(custom_icon_is_128, "Persist the custom icon at 128x128");
            require(icons.remove_custom_icon(custom_key), "Remove the custom icon override");
            require(!icons.has_custom_icon(custom_key), "Restore automatic icon selection");

            simpilot::MenuEntry chrome_profile{
                L"Chrome profile", L"chrome.exe --profile-directory=Profile1",
                simpilot::MenuEntryKind::command, 2};
            simpilot::MenuEntry chrome_app{
                L"Chrome app", L"chrome.exe --app-id=example",
                simpilot::MenuEntryKind::command, 3};
            const auto profile_key = simpilot::MenuIconCache::custom_key_for(chrome_profile);
            const auto app_key = simpilot::MenuIconCache::custom_key_for(chrome_app);
            require(profile_key != app_key,
                    "Keep different actions for the same executable independent");
            simpilot::MenuEntry case_sensitive_argument{
                L"Chrome app uppercase", L"chrome.exe --app-id=EXAMPLE",
                simpilot::MenuEntryKind::command, 4};
            require(simpilot::MenuIconCache::custom_key_for(case_sensitive_argument) != app_key,
                    "Preserve argument case in the configured action identity");
            chrome_profile.resolved_value = L"C:\\Resolved\\chrome.exe --profile-directory=Profile1";
            require(simpilot::MenuIconCache::custom_key_for(chrome_profile) == profile_key,
                    "Keep custom icons stable when program resolution changes");
            require(icons.set_custom_icon(profile_key,
                                          system_directory() / L"shell32.dll", 0),
                    "Set a custom icon for one executable action");
            require(icons.has_custom_icon(profile_key) && !icons.has_custom_icon(app_key),
                    "Do not apply a custom icon to another action using the same executable");
            require(icons.remove_custom_icon(profile_key),
                    "Remove the action-specific custom icon");

            simpilot::MenuEntry website{
                L"Website", L"https://example.test", simpilot::MenuEntryKind::web, 2};
            require(icons.icon_for(website) != nullptr,
                    "Load the Windows internet-shortcut icon");

            simpilot::LaunchMenuRenderer renderer;
            renderer.begin(simpilot::MenuTheme::dark, 96);
            const auto menu = CreatePopupMenu();
            require(renderer.append(menu, 1000, L"Large menu item", application_icon),
                    "Append an owner-drawn launch-menu item");
            MENUITEMINFOW item{
                .cbSize = sizeof(item),
                .fMask = MIIM_FTYPE | MIIM_DATA,
            };
            require(GetMenuItemInfoW(menu, 0, TRUE, &item) != FALSE
                    && (item.fType & MFT_OWNERDRAW) != 0 && item.dwItemData != 0,
                    "Store owner-draw metadata on the menu item");
            wchar_t access_label[64]{};
            MENUITEMINFOW access_information{
                .cbSize = sizeof(access_information),
                .fMask = MIIM_STRING,
                .dwTypeData = access_label,
                .cch = static_cast<UINT>(std::size(access_label) - 1),
            };
            require(renderer.append(menu, 1001, L"Access(&A)", application_icon)
                    && GetMenuItemInfoW(menu, 1, TRUE, &access_information) != FALSE
                    && std::wstring(access_label) == L"Access(&A)",
                    "Register the menu access key with the owner-drawn item");
            MEASUREITEMSTRUCT measurement{
                .CtlType = ODT_MENU,
                .itemData = item.dwItemData,
            };
            require(renderer.measure(measurement) && measurement.itemHeight >= 34,
                    "Measure a larger launch-menu item");
            require(renderer.append_separator(menu),
                    "Append a theme-aware owner-drawn separator");
            MENUITEMINFOW separator{
                .cbSize = sizeof(separator),
                .fMask = MIIM_FTYPE | MIIM_DATA,
            };
            require(GetMenuItemInfoW(menu, 2, TRUE, &separator) != FALSE
                    && (separator.fType & MFT_OWNERDRAW) != 0
                    && (separator.fType & MFT_SEPARATOR) == 0
                    && separator.dwItemData != 0,
                    "Use owner drawing instead of the native light separator");
            MEASUREITEMSTRUCT separator_measurement{
                .CtlType = ODT_MENU,
                .itemData = separator.dwItemData,
            };
            require(renderer.measure(separator_measurement)
                    && separator_measurement.itemHeight == 9,
                    "Measure the themed separator height");
            const auto menu_size = renderer.measure_menu(menu);
            require(menu_size.cx > 0 && menu_size.cy >= 43,
                    "Measure the complete launch menu");

            const RECT work_area{100, 100, 1100, 900};
            const SIZE popup_size{300, 400};
            require(simpilot::launch_menu_alignment(
                        POINT{200, 200}, work_area, popup_size)
                    == (TPM_LEFTALIGN | TPM_TOPALIGN),
                    "Open down and right when space is available");
            require(simpilot::launch_menu_alignment(
                        POINT{200, 700}, work_area, popup_size)
                    == (TPM_LEFTALIGN | TPM_BOTTOMALIGN),
                    "Open up and right when space below is insufficient");
            require(simpilot::launch_menu_alignment(
                        POINT{900, 200}, work_area, popup_size)
                    == (TPM_RIGHTALIGN | TPM_TOPALIGN),
                    "Open down and left when space to the right is insufficient");
            require(simpilot::launch_menu_alignment(
                        POINT{900, 700}, work_area, popup_size)
                    == (TPM_RIGHTALIGN | TPM_BOTTOMALIGN),
                    "Open up and left when lower-right space is insufficient");
            DestroyMenu(menu);
            renderer.end();
        }

        std::size_t cached_icons = 0;
        bool transparent_icon_found = false;
        if (std::filesystem::exists(cache_directory)) {
            for (const auto& entry : std::filesystem::directory_iterator(cache_directory)) {
                if (entry.is_regular_file() && entry.path().extension() == L".ico") {
                    ++cached_icons;
                    std::ifstream stream(entry.path(), std::ios::binary);
                    std::vector<unsigned char> bytes(
                        (std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
                    require(bytes.size() >= 22 && bytes[6] == 128 && bytes[7] == 128,
                            "Persist each cached icon at 128x128");
                    constexpr std::size_t pixel_offset = 6 + 16 + 40;
                    constexpr std::size_t pixel_bytes = 128 * 128 * 4;
                    if (bytes.size() >= pixel_offset + pixel_bytes) {
                        bool transparent = false;
                        bool visible = false;
                        for (std::size_t offset = pixel_offset + 3;
                             offset < pixel_offset + pixel_bytes; offset += 4) {
                            transparent = transparent || bytes[offset] == 0;
                            visible = visible || bytes[offset] != 0;
                        }
                        transparent_icon_found = transparent_icon_found
                            || (transparent && visible);
                    }
                }
            }
        }
        require(cached_icons >= 3, "Persist extracted icons under Cache\\RunIcon");
        require(transparent_icon_found, "Persist an icon with a transparent background");
        std::filesystem::remove_all(root);

        require(simpilot::MenuThemeController::apply(simpilot::MenuTheme::system),
                "Apply the system popup-menu theme");
        require(simpilot::MenuThemeController::apply(simpilot::MenuTheme::light),
                "Apply the light popup-menu theme");
        require(simpilot::MenuThemeController::apply(simpilot::MenuTheme::dark),
                "Apply the dark popup-menu theme");
        std::wcout << L"Menu presentation tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
