#include "launch_menu_renderer.hpp"
#include "menu_icon_cache.hpp"
#include "menu_parser.hpp"
#include "menu_theme.hpp"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
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

std::string file_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_test_image(const std::filesystem::path& path, REFGUID format,
                      UINT width, UINT height, bool transparent) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf()))), "Create image fixture factory");
    ComPtr<IWICStream> stream;
    require(SUCCEEDED(factory->CreateStream(stream.GetAddressOf()))
        && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)), "Open image fixture");
    ComPtr<IWICBitmapEncoder> encoder;
    require(SUCCEEDED(factory->CreateEncoder(format, nullptr, encoder.GetAddressOf()))
        && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)), "Initialize image encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    require(SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr))
        && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(width, height)),
        "Initialize image frame");
    WICPixelFormatGUID pixels_format = GUID_WICPixelFormat32bppBGRA;
    require(SUCCEEDED(frame->SetPixelFormat(&pixels_format)), "Select encoder pixel format");
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height,
        transparent ? 0x802A80E0U : 0xFF2A80E0U);
    ComPtr<IWICBitmap> bitmap;
    require(SUCCEEDED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGRA,
        width * 4, static_cast<UINT>(pixels.size() * 4),
        reinterpret_cast<BYTE*>(pixels.data()), bitmap.GetAddressOf())), "Create sample image pixels");
    ComPtr<IWICFormatConverter> converter;
    require(SUCCEEDED(factory->CreateFormatConverter(converter.GetAddressOf()))
        && SUCCEEDED(converter->Initialize(bitmap.Get(), pixels_format, WICBitmapDitherTypeNone,
            nullptr, 0, WICBitmapPaletteTypeMedianCut)), "Convert fixture to encoder format");
    require(SUCCEEDED(frame->WriteSource(converter.Get(), nullptr)) && SUCCEEDED(frame->Commit())
        && SUCCEEDED(encoder->Commit()), "Encode image fixture");
}

std::filesystem::path only_custom_icon(const std::filesystem::path& directory) {
    std::filesystem::path result;
    for (const auto& file : std::filesystem::directory_iterator(directory)) {
        if (!file.path().filename().wstring().ends_with(L".custom.ico")) continue;
        require(result.empty(), "Only one custom icon in isolated image fixture");
        result = file.path();
    }
    require(!result.empty(), "Image import writes a custom ICO");
    return result;
}

void check_image_imports(const std::filesystem::path& root) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    require(SUCCEEDED(com), "Initialize COM for image fixtures");
    struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } uninitialize;
    std::filesystem::create_directories(root);
    const std::array formats{
        std::pair{L"sample.png", &GUID_ContainerFormatPng},
        std::pair{L"sample.PNG", &GUID_ContainerFormatPng},
        std::pair{L"sample.jpg", &GUID_ContainerFormatJpeg},
        std::pair{L"sample.jpeg", &GUID_ContainerFormatJpeg},
        std::pair{L"sample.bmp", &GUID_ContainerFormatBmp},
        std::pair{L"sample.gif", &GUID_ContainerFormatGif},
        std::pair{L"sample.tif", &GUID_ContainerFormatTiff},
        std::pair{L"sample.tiff", &GUID_ContainerFormatTiff}};
    for (const auto& [name, format] : formats) {
        const auto source = root / name;
        write_test_image(source, *format, 4, 2, false);
        const auto directory = root / (std::wstring(name) + L"-cache");
        simpilot::MenuIconCache icons(directory);
        require(icons.set_custom_icon(L"image", source, 0), "Import each supported raster image format");
        const auto custom = only_custom_icon(directory);
        const auto data = file_bytes(custom);
        constexpr std::size_t pixel_offset = 6 + 16 + 40;
        require(data.size() > pixel_offset + 128 * 128 * 4
            && static_cast<unsigned char>(data[6]) == 128
            && static_cast<unsigned char>(data[7]) == 128, "Store raster imports in the existing 128px ICO format");
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                const auto alpha = static_cast<unsigned char>(
                    data[pixel_offset + (static_cast<std::size_t>(127 - y) * 128 + x) * 4 + 3]);
                require(alpha == (y >= 32 && y < 96 ? 255 : 0),
                    "Center wide artwork without stretching and leave transparent letterboxing");
            }
        }
        require(icons.icon_for_customization(L"image", source.wstring(),
            simpilot::MenuEntryKind::command) != nullptr, "Reload imported artwork as a Windows icon");
        std::filesystem::remove(source);
        simpilot::MenuIconCache reloaded(directory);
        require(reloaded.icon_for_customization(L"image", source.wstring(),
            simpilot::MenuEntryKind::command) != nullptr, "Imported icons do not depend on original image files");
        const auto original = file_bytes(custom);
        const auto corrupt = root / L"corrupt.png";
        { std::ofstream stream(corrupt, std::ios::binary); stream << "not an image"; }
        require(!icons.set_custom_icon(L"image", corrupt, 0) && file_bytes(custom) == original,
            "Malformed imports preserve the existing custom icon");
        const auto locked = CreateFileW(custom.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "Lock ICO to test failed atomic replacement");
        write_test_image(source, *format, 2, 4, false);
        require(!icons.set_custom_icon(L"image", source, 0) && file_bytes(custom) == original,
            "Save failure preserves the previous custom icon");
        CloseHandle(locked);
        require(icons.set_custom_icon(L"image", source, 0), "Retry image import after unlocking");
    }
    const auto source = root / L"alpha.png", directory = root / L"alpha-cache";
    write_test_image(source, GUID_ContainerFormatPng, 2, 4, true);
    simpilot::MenuIconCache icons(directory);
    require(icons.set_custom_icon(L"alpha", source, 0), "Import semitransparent PNG");
    const auto data = file_bytes(only_custom_icon(directory));
    constexpr std::size_t pixel_offset = 6 + 16 + 40;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const auto offset = pixel_offset + (static_cast<std::size_t>(127 - y) * 128 + x) * 4;
            const auto alpha = static_cast<unsigned char>(data[offset + 3]);
            require(alpha == (x >= 32 && x < 96 ? 128 : 0),
                "Portrait images retain partial alpha and transparent side margins");
            if (!alpha) continue;
            for (const auto& [component, expected] : std::array{
                std::pair{0, 224}, std::pair{1, 128}, std::pair{2, 42}}) {
                require(std::abs(static_cast<int>(static_cast<unsigned char>(
                    data[offset + component])) - expected) <= 2, "Alpha conversion does not darken source colors");
            }
        }
    }
    require(!icons.set_custom_icon(L"absent", root / L"missing.png", 0)
        && !icons.has_custom_icon(L"absent"), "Missing images do not create overrides");
}

void check_image_com_lifetime(const std::filesystem::path& root) {
    simpilot::MenuIconCache icons(root / L"com-cache");
    require(icons.set_custom_icon(L"uninitialized", root / L"alpha.png", 0),
        "Image import initializes COM when the caller has not done so");
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    require(com == S_OK, "Image import balances its own COM initialization");
    struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } uninitialize;
    require(icons.set_custom_icon(L"multithreaded", root / L"alpha.png", 0),
        "Image import respects the caller's existing multithreaded apartment");
    const auto existing = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(existing)) CoUninitialize();
    require(existing == S_FALSE, "Image import leaves the caller's apartment initialized");
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

void check_access_key_icon_identity(const std::filesystem::path& root) {
    auto document = simpilot::MenuParser::parse(
        L"Google Chrome(&H)|chrome.exe\nGoogle Chrome(&O)|chrome.exe --incognito\n"
        L"Google Chrome|chrome.exe\nGoogle Chrome(H)|chrome.exe\n");
    const auto entries = document.entries();
    const auto normal_key = simpilot::MenuIconCache::custom_key_for(*entries[0]);
    const auto private_key = simpilot::MenuIconCache::custom_key_for(*entries[1]);
    const auto plain_key = simpilot::MenuIconCache::custom_key_for(*entries[2]);
    require(entries[0]->display_name == entries[1]->display_name
        && entries[0]->access_key == L'H' && entries[1]->access_key == L'O',
        "Reproduce the user's two Chrome labels with separately parsed access keys");
    require(normal_key != private_key && normal_key != plain_key
        && normal_key != simpilot::MenuIconCache::custom_key_for(*entries[3]),
        "Full menu labels, including access keys, distinguish custom icon identities");
    require(simpilot::MenuIconCache::label_for(*entries[0]) == L"Google Chrome(H)"
        && simpilot::MenuIconCache::label_for(*entries[1]) == L"Google Chrome(O)",
        "Icon list names display the access key without the Win32 ampersand marker");
    entries[0]->value = L"other.exe --another-profile";
    entries[0]->resolved_value = executable_path().wstring();
    entries[0]->run_as_administrator = true;
    require(simpilot::MenuIconCache::custom_key_for(*entries[0]) == normal_key,
        "Target, arguments, resolution and administrator policy do not change a full menu-label identity");

    simpilot::MenuIconCache icons(root);
    require(icons.set_custom_icon(normal_key, system_directory() / L"shell32.dll", 0)
        && !icons.has_custom_icon(private_key), "Chrome(H) can have a custom icon independently of Chrome(O)");
    {
        simpilot::MenuIconCache reloaded(root);
        require(reloaded.has_custom_icon(normal_key) && !reloaded.has_custom_icon(private_key)
            && reloaded.icon_for(*entries[0]) != nullptr, "The full-label override is used by runtime menus after reload");
    }
    require(icons.remove_custom_icon(normal_key), "Clear independent override before migration");

    const auto shared_key = L"menu-name:" + entries[0]->display_name;
    const std::array<const simpilot::MenuEntry*, 3> migration_entries{
        entries[0], entries[1], entries[2]};
    require(icons.set_custom_icon(shared_key, system_directory() / L"shell32.dll", 0)
        && icons.set_custom_icon(private_key, system_directory() / L"shell32.dll", 1),
        "Seed the previous shared name-only icon and an explicit Chrome(O) choice");
    const auto custom_bytes = [&] {
        std::map<std::filesystem::path, std::string> result;
        for (const auto& file : std::filesystem::directory_iterator(root)) {
            if (!file.path().filename().wstring().ends_with(L".custom.ico")) continue;
            std::ifstream stream(file.path(), std::ios::binary);
            result.emplace(file.path(), std::string(
                std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()));
        }
        return result;
    };
    const auto original = custom_bytes();
    require(icons.migrate_legacy_custom_icons(migration_entries)
        && icons.has_custom_icon(normal_key) && icons.has_custom_icon(private_key)
        && icons.has_custom_icon(plain_key) && !icons.has_custom_icon(shared_key),
        "Migrate shared name-only data to keyed and unkeyed menu labels without retiring a live override");
    const auto migrated = custom_bytes();
    std::size_t preserved = 0;
    for (const auto& [path, bytes] : original) {
        if (const auto found = migrated.find(path); found != migrated.end()) {
            require(found->second == bytes, "Migration preserves the explicit Chrome(O) choice");
            ++preserved;
        }
    }
    require(preserved == 1, "Only the old shared identity is archived");
    require(icons.remove_custom_icon(normal_key)
        && icons.migrate_legacy_custom_icons(migration_entries)
        && !icons.has_custom_icon(normal_key) && icons.has_custom_icon(private_key)
        && icons.has_custom_icon(plain_key),
        "Restoring Chrome(H) remains automatic after reload and leaves Chrome(O) and the unkeyed label intact");
}

} // namespace

int wmain() {
    try {
        const auto root = std::filesystem::temp_directory_path()
            / (L"simpilot-menu-presentation-" + std::to_wstring(GetCurrentProcessId()));
        const auto cache_directory = root / L"Cache" / L"RunIcon";
        std::filesystem::remove_all(root);
        check_image_imports(root / L"Images");
        check_image_com_lifetime(root / L"Images");
        check_access_key_icon_identity(root / L"AccessKeyIcons");
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
                L"Chrome app", chrome_profile.value,
                simpilot::MenuEntryKind::command, 3};
            const auto profile_key = simpilot::MenuIconCache::custom_key_for(chrome_profile);
            const auto app_key = simpilot::MenuIconCache::custom_key_for(chrome_app);
            require(profile_key != app_key,
                    "Different menu names have independent icons even with identical commands");
            simpilot::MenuEntry case_sensitive_argument{
                L"Chrome app", L"another.exe --app-id=EXAMPLE",
                simpilot::MenuEntryKind::command, 4, true};
            require(simpilot::MenuIconCache::custom_key_for(case_sensitive_argument) == app_key,
                    "Changing target, arguments or privilege preserves menu-label identity");
            case_sensitive_argument.kind = simpilot::MenuEntryKind::web;
            require(simpilot::MenuIconCache::custom_key_for(case_sensitive_argument) == app_key,
                    "Changing action type does not reindex a named custom icon");
            case_sensitive_argument.access_key = L'C';
            require(simpilot::MenuIconCache::custom_key_for(case_sensitive_argument) != app_key,
                "An access key is part of the visible menu label and its icon identity");
            case_sensitive_argument.display_name = L"Renamed menu";
            require(simpilot::MenuIconCache::custom_key_for(case_sensitive_argument) != app_key,
                    "Menu name, not application name, defines the custom icon index");
            chrome_profile.resolved_value = L"C:\\Resolved\\chrome.exe --profile-directory=Profile1";
            require(simpilot::MenuIconCache::custom_key_for(chrome_profile) == profile_key,
                    "Keep custom icons stable when program resolution changes");
            require(icons.set_custom_icon(profile_key,
                                          system_directory() / L"shell32.dll", 0),
                    "Set a custom icon for one executable action");
            require(icons.has_custom_icon(profile_key) && !icons.has_custom_icon(app_key),
                    "Do not share custom icons across differently named menu items");
            {
                simpilot::MenuIconCache reloaded(cache_directory);
                require(reloaded.has_custom_icon(profile_key) && !reloaded.has_custom_icon(app_key),
                    "Name-based custom icon overrides persist independently");
                require(reloaded.icon_for(chrome_profile) == reloaded.icon_for_customization(
                    profile_key, *simpilot::MenuIconCache::target_for(chrome_profile),
                    chrome_profile.kind), "Runtime menus and settings previews use the same identity");
            }
            require(icons.remove_custom_icon(profile_key),
                    "Remove the name-specific custom icon");

            const auto legacy_key = L"command:normal:" + chrome_app.value;
            const std::array<const simpilot::MenuEntry*, 2> migration_entries{
                &chrome_profile, &chrome_app};
            require(icons.set_custom_icon(legacy_key, system_directory() / L"shell32.dll", 0),
                "Seed the pre-upgrade command-indexed custom icon");
            require(icons.set_custom_icon(app_key, system_directory() / L"shell32.dll", 1),
                "Seed an existing name-specific choice");
            const auto custom_files = [&] {
                std::map<std::filesystem::path, std::string> result;
                for (const auto& file : std::filesystem::directory_iterator(cache_directory)) {
                    if (!file.path().filename().wstring().ends_with(L".custom.ico")) continue;
                    std::ifstream stream(file.path(), std::ios::binary);
                    result.emplace(file.path(), std::string(
                        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()));
                }
                return result;
            };
            const auto existing_choices = custom_files();
            require(icons.migrate_legacy_custom_icons(migration_entries),
                "Migrate every menu alias before retiring the old command identity");
            require(icons.has_custom_icon(profile_key) && icons.has_custom_icon(app_key)
                && !icons.has_custom_icon(legacy_key), "Migration preserves both named overrides");
            for (const auto& [path, expected] : existing_choices) {
                if (!std::filesystem::exists(path)) continue;
                std::ifstream stream(path, std::ios::binary);
                require(std::string(std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>()) == expected,
                    "Migration does not overwrite an already selected name-specific icon");
            }
            require(icons.remove_custom_icon(profile_key)
                && icons.migrate_legacy_custom_icons(migration_entries)
                && !icons.has_custom_icon(profile_key) && icons.has_custom_icon(app_key),
                "Restore automatic remains automatic after reload without changing another menu name");
            require(icons.remove_custom_icon(app_key), "Clean up the other named override");

            require(icons.set_custom_icon(legacy_key, system_directory() / L"shell32.dll", 0),
                "Seed another legacy migration");
            std::filesystem::path legacy_path;
            for (const auto& file : std::filesystem::directory_iterator(cache_directory)) {
                if (file.path().filename().wstring().ends_with(L".custom.ico"))
                    legacy_path = file.path();
            }
            require(!legacy_path.empty(), "Locate legacy data for a sharing-violation test");
            const auto locked = CreateFileW(legacy_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, 0, nullptr);
            require(locked != INVALID_HANDLE_VALUE, "Lock migration source");
            require(!icons.migrate_legacy_custom_icons(migration_entries)
                && icons.has_custom_icon(legacy_key) && !icons.has_custom_icon(profile_key)
                && !icons.has_custom_icon(app_key), "Failed migration leaves legacy data and no partial name overrides");
            CloseHandle(locked);
            require(icons.remove_custom_icon(legacy_key), "Remove test migration source");

            simpilot::MenuEntry same_name_application{
                L"Shared name", executable_path().wstring(), simpilot::MenuEntryKind::command, 5};
            simpilot::MenuEntry same_name_website{
                L"Shared name", L"https://example.test", simpilot::MenuEntryKind::web, 6};
            require(icons.icon_for(same_name_application) != icons.icon_for(same_name_website),
                "Automatic icons with identical labels still follow their own target sources");

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
