#include "launch_menu_renderer.hpp"
#include "menu_icon_cache.hpp"
#include "menu_theme.hpp"

#include <Windows.h>

#include <algorithm>
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

MEASUREITEMSTRUCT measure_item(const simpilot::LaunchMenuRenderer& renderer,
                              const HMENU menu, const UINT index) {
    MENUITEMINFOW item{.cbSize = sizeof(item), .fMask = MIIM_DATA};
    require(GetMenuItemInfoW(menu, index, TRUE, &item) != FALSE, "Read item metadata");
    MEASUREITEMSTRUCT measurement{.CtlType = ODT_MENU, .itemData = item.dwItemData};
    require(renderer.measure(measurement), "Measure an owner-drawn item");
    return measurement;
}

void check_menu_geometry(const HICON icon) {
    for (const auto dpi : {96u, 144u, 192u}) {
        const auto scale = [dpi](const int dip) { return MulDiv(dip, dpi, 96); };
        const auto font = CreateFontW(-scale(15), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        require(font != nullptr, "Create the reference 15-DIP font");
        const auto dc = CreateCompatibleDC(nullptr);
        require(dc != nullptr, "Create the reference text measurement DC");
        const auto previous = SelectObject(dc, font);
        const auto text_size = [dc](const std::wstring_view text) {
            RECT bounds{};
            DrawTextW(dc, text.data(), static_cast<int>(text.size()), &bounds,
                DT_CALCRECT | DT_SINGLELINE);
            return SIZE{bounds.right - bounds.left, bounds.bottom - bounds.top};
        };
        for (const auto theme : {simpilot::MenuTheme::light, simpilot::MenuTheme::dark}) {
            simpilot::LaunchMenuRenderer renderer;
            renderer.begin(theme, dpi);
            const auto root = CreatePopupMenu();
            const auto child = CreatePopupMenu();
            require(root && child, "Create independent parent and child menus");
            const std::vector<std::wstring> labels{
                L"A", L"Tools / \u5de5\u5177",
                L"Review documents / \u5ba1\u6838\u6587\u6863",
                L"Extremely long application name " + std::wstring(160, L'W'),
                std::wstring(100, L'\u6587'), L"Access(&A)", L"Literal && ampersand"};
            UINT index = 0;
            for (const auto& label : labels) {
                require(renderer.append(root, 2000 + index, label, icon), "Append sizing sample");
                const auto actual = measure_item(renderer, root, index++);
                const auto text = text_size(label);
                require(actual.itemWidth == static_cast<UINT>(
                    std::min(text.cx + scale(44) + scale(16), static_cast<LONG>(scale(360)))),
                    "Size to actual text plus icon slot and padding, with only an upper cap");
                require(actual.itemHeight == static_cast<UINT>(
                    std::max(static_cast<LONG>(scale(34)), text.cy + scale(14))),
                    "Restore the original row height at each DPI");
            }
            require(measure_item(renderer, root, 0).itemWidth < static_cast<UINT>(scale(280)),
                "Do not force short names to the old 280-DIP minimum");
            require(measure_item(renderer, root, 3).itemWidth == static_cast<UINT>(scale(360))
                && measure_item(renderer, root, 4).itemWidth == static_cast<UINT>(scale(360)),
                "Cap both English and Chinese oversized labels");
            require(renderer.append(root, 2100, labels[1], nullptr), "Append iconless sample");
            require(measure_item(renderer, root, index++).itemWidth
                == measure_item(renderer, root, 1).itemWidth, "Keep the icon slot when no icon exists");
            require(renderer.append(child, 2200, L"A", icon), "Append a short child item");
            require(renderer.append(root, 2300, labels[1], icon, child), "Append a submenu sample");
            require(measure_item(renderer, root, index++).itemWidth
                == measure_item(renderer, root, 1).itemWidth + scale(28) - scale(16),
                "Reserve the submenu arrow separately from command padding");
            require(renderer.measure_menu(child).cx < renderer.measure_menu(root).cx,
                "Measure the child independently of the wide parent");
            require(renderer.append_separator(root), "Append a separator sizing sample");
            const auto separator = measure_item(renderer, root, index);
            require(separator.itemHeight == static_cast<UINT>(scale(9))
                && separator.itemWidth == 0, "Keep separators from expanding the menu");
            DestroyMenu(root);
        }
        SelectObject(dc, previous);
        DeleteDC(dc);
        DeleteObject(font);
    }
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
            check_menu_geometry(application_icon);

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
            const auto native_menu = CreatePopupMenu();
            require(native_menu != nullptr, "Create a native context menu");
            require(AppendMenuW(native_menu, MF_STRING, 1002, L"Native item") != FALSE,
                    "Append a native context-menu item");
            const auto native_menu_size = renderer.measure_menu(native_menu);
            require(native_menu_size.cx == 0 && native_menu_size.cy == 0,
                    "Leave native menu measurement to the system fallback");
            DestroyMenu(native_menu);
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
            require(renderer.measure(measurement) && measurement.itemHeight >= 34
                && measurement.itemWidth < 280, "Restore compact, content-sized menu proportions");
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
