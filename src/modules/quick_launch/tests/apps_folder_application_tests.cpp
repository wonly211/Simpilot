#include "application_source_dialog.hpp"
#include "apps_folder_application_catalog.hpp"
#include "apps_folder_application_dialog.hpp"
#include "menu_icon_cache.hpp"

#include <Windows.h>
#include <commctrl.h>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr auto dialog_class_name = L"Simpilot.AppsFolderApplicationDialog";
constexpr auto source_dialog_class_name = L"Simpilot.ApplicationSourceDialog";
constexpr int application_list_identifier = 100;
constexpr int packaged_radio_identifier = 100;
constexpr int executable_radio_identifier = 101;
constexpr int apps_folder_radio_identifier = 102;
std::filesystem::path screenshot_directory;

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool capture_window(const HWND window, const std::filesystem::path& destination) {
    RECT rectangle{};
    if (!GetWindowRect(window, &rectangle)) return false;
    const auto width = rectangle.right - rectangle.left;
    const auto height = rectangle.bottom - rectangle.top;
    const auto window_dc = GetWindowDC(window);
    if (!window_dc) return false;
    const auto memory_dc = CreateCompatibleDC(window_dc);
    const auto bitmap = CreateCompatibleBitmap(window_dc, width, height);
    if (!memory_dc || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memory_dc) DeleteDC(memory_dc);
        ReleaseDC(window, window_dc);
        return false;
    }
    const auto previous = SelectObject(memory_dc, bitmap);
    auto rendered = PrintWindow(window, memory_dc, 0x00000002) != FALSE;
    if (!rendered) {
        constexpr LPARAM print_flags = PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN
            | PRF_ERASEBKGND | PRF_OWNED;
        SendMessageW(window, WM_PRINT, reinterpret_cast<WPARAM>(memory_dc), print_flags);
        rendered = true;
    }
    BITMAPINFO information{};
    information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    information.bmiHeader.biWidth = width;
    information.bmiHeader.biHeight = -height;
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;
    std::vector<std::byte> pixels(
        static_cast<std::size_t>(width) * 4 * static_cast<std::size_t>(height));
    SelectObject(memory_dc, previous);
    const auto copied = GetDIBits(window_dc, bitmap, 0, static_cast<UINT>(height),
        pixels.data(), &information, DIB_RGB_COLORS) != 0;
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(window, window_dc);
    if (!rendered || !copied) return false;

    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4D42;
    file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = file_header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
    output.write(reinterpret_cast<const char*>(&information.bmiHeader),
        sizeof(information.bmiHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()),
        static_cast<std::streamsize>(pixels.size()));
    return output.good();
}

void verify_application_source_dialog() {
    const auto main_thread = GetCurrentThreadId();
    std::atomic ui_verified = false;
    std::thread automation([main_thread, &ui_verified] {
        HWND dialog = nullptr;
        for (int attempt = 0; attempt < 200 && !dialog; ++attempt) {
            dialog = FindWindowW(source_dialog_class_name, nullptr);
            if (!dialog) Sleep(25);
        }
        HWND packaged = nullptr;
        HWND executable = nullptr;
        HWND apps_folder = nullptr;
        HWND select = nullptr;
        HWND cancel = nullptr;
        for (int attempt = 0; attempt < 400; ++attempt) {
            packaged = GetDlgItem(dialog, packaged_radio_identifier);
            executable = GetDlgItem(dialog, executable_radio_identifier);
            apps_folder = GetDlgItem(dialog, apps_folder_radio_identifier);
            select = GetDlgItem(dialog, IDOK);
            cancel = GetDlgItem(dialog, IDCANCEL);
            if (packaged && executable && apps_folder && select && cancel
                && SendMessageW(executable, BM_GETCHECK, 0, 0) == BST_CHECKED) break;
            Sleep(5);
        }
        const auto dpi = dialog ? GetDpiForWindow(dialog) : 96;
        const auto scale = [dpi](const int value) { return MulDiv(value, dpi, 96); };
        RECT packaged_bounds{}, executable_bounds{}, apps_folder_bounds{};
        RECT select_bounds{}, cancel_bounds{}, title_bounds{}, client_bounds{};
        if (packaged) GetWindowRect(packaged, &packaged_bounds);
        if (executable) GetWindowRect(executable, &executable_bounds);
        if (apps_folder) GetWindowRect(apps_folder, &apps_folder_bounds);
        if (select) GetWindowRect(select, &select_bounds);
        if (cancel) GetWindowRect(cancel, &cancel_bounds);
        if (dialog) GetClientRect(dialog, &client_bounds);
        HWND title = nullptr;
        for (auto child = dialog ? GetWindow(dialog, GW_CHILD) : nullptr;
             child; child = GetWindow(child, GW_HWNDNEXT)) {
            wchar_t class_name[16]{};
            GetClassNameW(child, class_name, static_cast<int>(std::size(class_name)));
            if (_wcsicmp(class_name, L"STATIC") == 0 && GetWindowTextLengthW(child) > 0) {
                title = child;
                break;
            }
        }
        LOGFONTW title_font{};
        const auto title_handle = title
            ? reinterpret_cast<HFONT>(SendMessageW(title, WM_GETFONT, 0, 0)) : nullptr;
        if (title) GetWindowRect(title, &title_bounds);
        if (title && dialog) {
            MapWindowPoints(HWND_DESKTOP, dialog,
                reinterpret_cast<POINT*>(&title_bounds), 2);
        }
        if (select && dialog) {
            MapWindowPoints(HWND_DESKTOP, dialog,
                reinterpret_cast<POINT*>(&select_bounds), 2);
        }
        if (cancel && dialog) {
            MapWindowPoints(HWND_DESKTOP, dialog,
                reinterpret_cast<POINT*>(&cancel_bounds), 2);
        }
        const auto title_font_valid = title_handle
            && GetObjectW(title_handle, sizeof(title_font), &title_font)
            && title_font.lfHeight == -scale(24);
        const auto button_size = [scale](const RECT& bounds) {
            return bounds.right - bounds.left == scale(88)
                && bounds.bottom - bounds.top == scale(36);
        };
        ui_verified = packaged && executable && apps_folder && select && cancel
            && SendMessageW(executable, BM_GETCHECK, 0, 0) == BST_CHECKED
            && title_font_valid
            && title_bounds.left == scale(28) && title_bounds.top == scale(28)
            && packaged_bounds.top < executable_bounds.top
            && executable_bounds.top < apps_folder_bounds.top
            && cancel_bounds.left == scale(28)
            && client_bounds.right - select_bounds.right == scale(28)
            && client_bounds.bottom - select_bounds.bottom == scale(18)
            && button_size(select_bounds) && button_size(cancel_bounds);
        if (!ui_verified) {
            if (dialog) PostMessageW(dialog, WM_CLOSE, 0, 0);
            return;
        }
        if (!screenshot_directory.empty()) {
            std::error_code error;
            std::filesystem::create_directories(screenshot_directory, error);
            if (!error) {
                (void)capture_window(
                    dialog, screenshot_directory / L"application-source.bmp");
            }
        }
        PostMessageW(apps_folder, BM_CLICK, 0, 0);
        PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    });
    const auto selected = simpilot::ApplicationSourceDialog::show_modal(
        GetModuleHandleW(nullptr), nullptr, "en-US");
    automation.join();
    require(ui_verified, "Application source dialog follows the shared visual baseline");
    require(selected == simpilot::ApplicationSource::apps_folder,
        "Application source dialog returns the selected source");
}

void verify_shell_namespace_icons(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    simpilot::MenuIconCache icons(root);
    const auto packaged = simpilot::AppsFolderApplicationCatalog::enumerate(
        simpilot::AppsFolderApplicationScope::packaged);
    if (!packaged.empty()) {
        const simpilot::MenuEntry entry(
            L"Installed app",
            simpilot::AppsFolderApplicationCatalog::shell_target(packaged.front().aumid),
            simpilot::MenuEntryKind::command, 1);
        require(icons.icon_for(entry) != nullptr,
            "Extract an icon from shell:AppsFolder");
    }
    const simpilot::MenuEntry settings(
        L"Settings", L"ms-settings:", simpilot::MenuEntryKind::command, 1);
    require(icons.icon_for(settings) != nullptr,
        "Extract an icon from ms-settings:");
    const simpilot::MenuEntry control_panel(
        L"Control Panel", L"::{26EE0668-A00A-44D7-9371-BEB064C98683}",
        simpilot::MenuEntryKind::command, 1);
    require(icons.icon_for(control_panel) != nullptr,
        "Extract an icon from a shell GUID target");
}

} // namespace

int run_tests() {
    using simpilot::AppsFolderApplication;
    using simpilot::AppsFolderApplicationCatalog;
    using simpilot::AppsFolderApplicationScope;

    const AppsFolderApplication calculator{
        L"Calculator",
        L"Microsoft.WindowsCalculator_8wekyb3d8bbwe",
        L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
    };
    const AppsFolderApplication chatgpt{
        L"ChatGPT Classic",
        L"OpenAI.ChatGPT-Desktop_2p2nqsd0c76g0",
        L"OpenAI.ChatGPT-Desktop_2p2nqsd0c76g0!ChatGPT",
    };
    const AppsFolderApplication settings{
        L"Settings",
        L"windows.immersivecontrolpanel_cw5n1h2txyewy",
        L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel",
    };
    const AppsFolderApplication desktop_entry{
        L"7-Zip",
        L"",
        L"{6D809377-6AF0-444B-8957-A3773F02200E}\\7-Zip\\7zFM.exe",
    };

    require(AppsFolderApplicationCatalog::is_launchable(desktop_entry),
        "Accept a launchable desktop AppsFolder entry");
    require(AppsFolderApplicationCatalog::is_packaged(calculator),
        "Accept a package-family application");
    require(!AppsFolderApplicationCatalog::is_packaged(desktop_entry),
        "Reject an unpackaged AppsFolder entry");
    std::cout << "Classification verified\n" << std::flush;

    const auto packaged = AppsFolderApplicationCatalog::normalize({
        settings, desktop_entry, calculator, chatgpt, calculator},
        AppsFolderApplicationScope::packaged);
    require(packaged.size() == 3, "Filter desktop entries from the packaged application list");
    require(packaged[0] == calculator && packaged[1] == chatgpt
        && packaged[2] == settings, "Sort packaged applications by display name");
    const auto all = AppsFolderApplicationCatalog::normalize({
        settings, desktop_entry, calculator, chatgpt, calculator},
        AppsFolderApplicationScope::all);
    require(all.size() == 4 && all[0] == desktop_entry
        && all[1] == calculator && all[2] == chatgpt && all[3] == settings,
        "Keep launchable desktop apps in the full AppsFolder list");
    require(AppsFolderApplicationCatalog::shell_target(calculator.aumid)
        == L"shell:AppsFolder\\" + calculator.aumid, "Build the shell AppsFolder target");
    std::cout << "Normalization verified\n" << std::flush;

    const auto installed_packaged = AppsFolderApplicationCatalog::enumerate(
        AppsFolderApplicationScope::packaged);
    for (const auto& application : installed_packaged) {
        require(AppsFolderApplicationCatalog::is_packaged(application),
            "Native AppsFolder enumeration returns only packaged applications");
    }
    const auto installed_all = AppsFolderApplicationCatalog::enumerate(
        AppsFolderApplicationScope::all);
    for (const auto& application : installed_all) {
        require(AppsFolderApplicationCatalog::is_launchable(application),
            "Native AppsFolder enumeration returns only launchable entries");
    }
    require(installed_all.size() >= installed_packaged.size(),
        "Full AppsFolder list includes packaged applications");
    std::cout << "Native AppsFolder packaged applications: " << installed_packaged.size()
        << ", all applications: " << installed_all.size() << '\n' << std::flush;

    for (const auto locale : {"en-US", "zh-CN", "zh-TW"}) {
        const simpilot::Localization localization(locale);
        for (const auto key : {"menu_editor.choose_application_source",
                 "menu_editor.application_source.subtitle",
                 "menu_editor.application_source.file",
                 "menu_editor.application_source.file_description",
                 "menu_editor.application_source.packaged",
                 "menu_editor.application_source.packaged_description",
                 "menu_editor.application_source.apps_folder",
                 "menu_editor.application_source.apps_folder_description",
                 "menu_editor.application_source.continue",
                 "menu_editor.no_packaged_applications",
                 "menu_editor.no_apps_folder_applications",
                 "apps_folder_application.window_title.packaged",
                 "apps_folder_application.window_title.all",
                 "apps_folder_application.prompt.packaged",
                 "apps_folder_application.prompt.all",
                 "apps_folder_application.name",
                 "apps_folder_application.package",
                 "apps_folder_application.aumid",
                 "apps_folder_application.desktop"}) {
            require(localization.text(key) != L"[missing translation]",
                "Packaged application translations are available");
        }
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto icons_root = std::filesystem::temp_directory_path()
        / (L"simpilot-shell-icons-" + std::to_wstring(GetCurrentProcessId()));
    verify_shell_namespace_icons(icons_root);
    std::filesystem::remove_all(icons_root);
    verify_application_source_dialog();

    const std::vector<AppsFolderApplication> applications{calculator, chatgpt, settings};
    const auto main_thread = GetCurrentThreadId();
    std::atomic ui_verified = false;
    std::thread automation([main_thread, &ui_verified] {
        HWND dialog = nullptr;
        for (int attempt = 0; attempt < 200 && !dialog; ++attempt) {
            dialog = FindWindowW(dialog_class_name, nullptr);
            if (!dialog) Sleep(25);
        }
        if (!dialog) {
            PostThreadMessageW(main_thread, WM_QUIT, 0, 0);
            return;
        }
        HWND list = nullptr;
        for (int attempt = 0; attempt < 400; ++attempt) {
            list = GetDlgItem(dialog, application_list_identifier);
            if (list && ListView_GetItemCount(list) == 3
                && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 1) break;
            Sleep(5);
        }
        const auto header = list ? ListView_GetHeader(list) : nullptr;
        const auto selected = list
            ? ListView_GetNextItem(list, -1, LVNI_SELECTED) : -1;
        ui_verified = list && header
            && Header_GetItemCount(header) == 3
            && ListView_GetItemCount(list) == 3
            && selected == 1;
        if (!ui_verified) {
            PostMessageW(dialog, WM_CLOSE, 0, 0);
            return;
        }
        PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    });

    const auto selected = simpilot::AppsFolderApplicationDialog::show_modal(
        GetModuleHandleW(nullptr), nullptr, "en-US", applications,
        AppsFolderApplicationScope::packaged,
        AppsFolderApplicationCatalog::shell_target(chatgpt.aumid));
    automation.join();
    require(ui_verified, "Packaged application dialog rendered and preselected the current app");
    require(selected && *selected == chatgpt.aumid,
        "Packaged application dialog returns the selected AUMID");

    const std::vector<AppsFolderApplication> all_applications{
        desktop_entry, calculator, chatgpt, settings};
    std::atomic all_ui_verified = false;
    std::thread all_automation([main_thread, &all_ui_verified] {
        HWND dialog = nullptr;
        for (int attempt = 0; attempt < 200 && !dialog; ++attempt) {
            dialog = FindWindowW(dialog_class_name, nullptr);
            if (!dialog) Sleep(25);
        }
        if (!dialog) {
            PostThreadMessageW(main_thread, WM_QUIT, 0, 0);
            return;
        }
        HWND list = nullptr;
        for (int attempt = 0; attempt < 400; ++attempt) {
            list = GetDlgItem(dialog, application_list_identifier);
            if (list && ListView_GetItemCount(list) == 4
                && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0) break;
            Sleep(5);
        }
        wchar_t package[128]{};
        if (list) {
            ListView_GetItemText(list, 0, 1, package,
                static_cast<int>(std::size(package)));
        }
        const simpilot::Localization localization("en-US");
        all_ui_verified = list && ListView_GetItemCount(list) == 4
            && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0
            && std::wstring_view(package)
                == localization.text("apps_folder_application.desktop");
        if (!all_ui_verified) {
            PostMessageW(dialog, WM_CLOSE, 0, 0);
            return;
        }
        PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    });
    const auto selected_all = simpilot::AppsFolderApplicationDialog::show_modal(
        GetModuleHandleW(nullptr), nullptr, "en-US", all_applications,
        AppsFolderApplicationScope::all);
    all_automation.join();
    require(all_ui_verified, "Full AppsFolder dialog lists desktop entries");
    require(selected_all && *selected_all == desktop_entry.aumid,
        "Full AppsFolder dialog returns the selected desktop entry");
    std::cout << "AppsFolder application tests passed\n" << std::flush;
    return 0;
}

int wmain(const int argument_count, wchar_t** arguments) {
    try {
        if (argument_count == 3 && std::wstring_view(arguments[1]) == L"--screenshots") {
            screenshot_directory = arguments[2];
        }
        return run_tests();
    } catch (const std::exception& error) {
        std::cerr << "Packaged application test failed: " << error.what() << '\n';
        return 1;
    }
}
