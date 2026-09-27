#include "quick_launch_module.hpp"
#include "quick_launch_settings_ui.hpp"
#include "simpilot/config_file.hpp"
#include <commctrl.h>
#include <fstream>
#include <iostream>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::vector<std::filesystem::path> matching(const std::filesystem::path& root, std::wstring_view prefix) {
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::directory_iterator(root))
        if (entry.path().filename().wstring().starts_with(prefix)) result.push_back(entry.path());
    return result;
}
BOOL CALLBACK dismiss_error(HWND window, LPARAM) {
    wchar_t name[40]{};
    GetClassNameW(window, name, 40);
    if (std::wstring_view(name) == L"#32770") {
        EndDialog(window, GetDlgItem(window, IDYES) ? IDYES : IDOK);
    }
    return TRUE;
}
void CALLBACK dismiss_timer(HWND, UINT, UINT_PTR, DWORD) {
    EnumThreadWindows(GetCurrentThreadId(), dismiss_error, 0);
}
void codec() {
    auto document = simpilot::SettingsDocument::parse("; unknown\n[Future]\nKeep=1\n");
    simpilot::QuickLaunchSettings settings;
    require(settings.main_menu.enabled && settings.main_menu.binding.gesture->virtual_key == VK_OEM_3,
        "Default launcher binding remains backtick");
    settings.menu_theme = simpilot::MenuTheme::dark;
    settings.main_menu = {{simpilot::HotKeyGesture{MOD_CONTROL, VK_MEDIA_PLAY_PAUSE}, true}, true};
    settings.second_menu = {{simpilot::HotKeyGesture{MOD_WIN | MOD_SHIFT, VK_F12}, false}, false};
    settings.write(document);
    require(simpilot::QuickLaunchSettings::read(document) == settings, "Theme, media key and disabled shortcut round-trip");
    require(document.get(L"Keep") == L"1", "Preserve absent-module content");
    for (const auto locale : {"en-US", "zh-CN", "zh-TW"}) {
        simpilot::Localization localization(locale);
        for (const auto key : {"ui.edit_menus", "ui.reload_menu", "settings.menu_theme",
            "settings.tab.quick_launch", "settings.tab.menu_icons", "menu_editor.save_failed"}) {
            require(localization.text(key) != L"[missing translation]", "Module translations available");
        }
    }
}
void transaction(const std::filesystem::path& root, int failure) {
    std::cout << "Transaction fixture " << failure << '\n' << std::flush;
    const auto config = root / L"Config";
    const auto icons = root / L"Cache" / L"RunIcon";
    std::filesystem::create_directories(config);
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const std::wstring command = L"\"" + std::wstring(executable) + L"\"";
    const auto menu = config / L"Simpilot.ini";
    simpilot::write_configuration_text(menu, L"Original|" + command + L"\n");
    simpilot::MenuEntry entry(L"Original", command, simpilot::MenuEntryKind::command, 1);
    const auto key = simpilot::MenuIconCache::custom_key_for(entry);
    simpilot::MenuIconCache original_icons(icons);
    require(original_icons.set_custom_icon(key, executable, 0), "Seed original custom icon");
    const auto original_menu = bytes(menu);
    auto files = std::filesystem::directory_iterator(icons);
    std::filesystem::path original_icon;
    for (const auto& file : files) if (file.path().extension() == L".ico") original_icon = file.path();
    require(!original_icon.empty(), "Find original custom icon");
    const auto original_image = bytes(original_icon);
    simpilot::QuickLaunchSettings draft;
    auto ui = simpilot::make_quick_launch_settings_ui(draft, config, icons,
        [&] { return std::vector<simpilot::MenuIconTarget>{
            {L"Main", L"Original", command, key, executable, simpilot::MenuEntryKind::command}}; },
        {}, {}, {});
    simpilot::SettingsParticipantRegistry participants;
    auto registration = participants.add("quick_launch", 0, {
        .dirty = [&] { return ui->dirty(); }, .prepare = [&] { return ui->prepare(); },
        .apply = [&] { return ui->apply(); },
        .write = [&](auto& document) { draft.write(document); },
        .rollback = [&] { return ui->rollback(); }, .finish = [&] { ui->finish(); }});
    simpilot::SettingsSession session(participants, {});
    const auto instance = GetModuleHandleW(nullptr);
    auto parent = CreateWindowW(L"STATIC", L"Isolated quick launch test", WS_OVERLAPPEDWINDOW,
        0, 0, 1050, 760, nullptr, nullptr, instance, nullptr);
    const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    simpilot::Localization localization("en-US");
    auto editor_page = ui->page(false);
    auto icon_page = ui->page(true);
    editor_page->create({instance, parent, 96, font, localization, [] {}});
    std::cout << "Editor created\n" << std::flush;
    icon_page->create({instance, parent, 96, font, localization, [] {}});
    auto editor_parent = FindWindowExW(parent, nullptr, L"Simpilot.QuickLaunchSettingsPage", nullptr);
    auto icon_parent = FindWindowExW(parent, editor_parent, L"Simpilot.QuickLaunchSettingsPage", nullptr);
    const auto editor = FindWindowExW(editor_parent, nullptr, L"Simpilot.MenuEditorWindow", nullptr);
    require(editor && icon_parent, "Both settings pages created");
    for (auto dpi : {96u, 144u, 192u}) {
        editor_page->layout({0, 0, MulDiv(780, dpi, 96), MulDiv(600, dpi, 96)}, dpi, font);
        icon_page->layout({0, 0, MulDiv(780, dpi, 96), MulDiv(600, dpi, 96)}, dpi, font);
        const auto list = GetDlgItem(icon_parent, 500);
        int icon_width = 0, icon_height = 0;
        ImageList_GetIconSize(ListView_GetImageList(list, LVSIL_SMALL), &icon_width, &icon_height);
        require(icon_width == MulDiv(32, dpi, 96) && icon_height == MulDiv(36, dpi, 96),
            "Menu icon previews follow page DPI changes");
        RECT client{}, button{};
        GetClientRect(list, &client);
        int columns = 0;
        for (int column = 0; column < 4; ++column) columns += ListView_GetColumnWidth(list, column);
        require(columns >= MulDiv(632, dpi, 96),
            "Icon columns preserve readable minimum widths with horizontal scrolling");
        GetWindowRect(GetDlgItem(icon_parent, 501), &button);
        require(button.bottom - button.top == MulDiv(32, dpi, 96)
            && button.right - button.left == MulDiv(120, dpi, 96),
            "Icon action uses a compact 32dip toolbar button");
    }
    const auto editor_body = GetWindow(GetWindow(editor, GW_CHILD), GW_CHILD);
    const auto tree = GetDlgItem(editor_body, 101);
    TreeView_SelectItem(tree, TreeView_GetRoot(tree));
    SetWindowTextW(GetDlgItem(editor_body, 300), L"Edited");
    const auto list = GetDlgItem(icon_parent, 500);
    require(ListView_GetItemCount(list) == 1, "Icon page enumerates real menu target");
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(icon_parent, WM_COMMAND, MAKEWPARAM(502, BN_CLICKED), 0);
    require(session.dirty(), "Editor and icon drafts are isolated");
    std::cout << "Drafts edited\n" << std::flush;
    require(bytes(menu) == original_menu && bytes(original_icon) == original_image,
        "Editing does not modify live menu or icons");
    if (failure == 1) {
        const auto locked = CreateFileW(original_icon.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "Lock live icon to inject rollback failure");
        require(!session.apply([](const auto&) { return true; }), "Locked icon blocks transaction");
        CloseHandle(locked);
        require(!matching(root / L"Cache", L"SettingsRecovery-").empty(),
            "Rollback failure retains disk recovery data");
        require(!session.apply([](const auto&) { return true; }), "Do not overwrite unresolved recovery data");
    } else if (failure == 2) {
        const auto snapshots = matching(root / L"Cache", L"SettingsDraft-");
        require(snapshots.size() == 1, "Find isolated icon draft");
        const auto injected = snapshots.front() / original_icon.filename();
        std::filesystem::copy_file(original_icon, injected);
        const auto locked = CreateFileW(injected.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "Lock draft icon to inject copy failure");
        require(!session.apply([](const auto&) { return true; }), "Icon failure aborts transaction");
        CloseHandle(locked);
        require(bytes(menu) == original_menu && bytes(original_icon) == original_image,
            "Icon failure restores both menu and original icon");
        std::filesystem::remove(injected);
        require(session.apply([&](const auto& doc) { return doc.save(config / L"Setting.ini"); }),
            "Retry succeeds after icon failure");
        require(!std::filesystem::exists(original_icon) && !session.dirty(),
            "Committed icon removal clears draft without losing the menu change");
    } else {
        const auto locked = CreateFileW(menu.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "Lock menu to inject menu save failure");
        const auto timer = SetTimer(nullptr, 0, 20, dismiss_timer);
        require(!session.apply([](const auto&) { return true; }), "Menu write failure aborts transaction");
        KillTimer(nullptr, timer);
        CloseHandle(locked);
        // A locked menu also blocks its restoration, so use a fresh recovery session.
        require(bytes(menu) == original_menu, "Failed menu replacement does not truncate original");
    }
    editor_page.reset(); icon_page.reset(); DestroyWindow(parent);
    session.cancel(); ui.reset();
}
void ini_and_icon_failures(const std::filesystem::path& root) {
    std::cout << "INI failure fixture\n" << std::flush;
    const auto config = root / L"Config", icons = root / L"Cache" / L"RunIcon";
    std::filesystem::create_directories(config);
    simpilot::write_configuration_text(config / L"Simpilot.ini", L"Original|notepad.exe\n");
    simpilot::QuickLaunchSettings settings;
    auto ui = simpilot::make_quick_launch_settings_ui(settings, config, icons,
        [] { return std::vector<simpilot::MenuIconTarget>{}; }, {}, {}, {});
    const auto instance = GetModuleHandleW(nullptr);
    const auto parent = CreateWindowW(L"STATIC", L"", WS_OVERLAPPEDWINDOW,
        0, 0, 1000, 700, nullptr, nullptr, instance, nullptr);
    simpilot::Localization language("en-US");
    auto page = ui->page(false);
    page->create({instance, parent, 96, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), language, [] {}});
    page->layout({0, 0, 900, 600}, 96, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    const auto child = FindWindowExW(parent, nullptr, L"Simpilot.QuickLaunchSettingsPage", nullptr);
    const auto editor = FindWindowExW(child, nullptr, L"Simpilot.MenuEditorWindow", nullptr);
    const auto editor_body = GetWindow(GetWindow(editor, GW_CHILD), GW_CHILD);
    require(GetDlgItem(editor_body, 101) && GetDlgItem(editor_body, 300), "Locate scrollable editor fields");
    TreeView_SelectItem(GetDlgItem(editor_body, 101), TreeView_GetRoot(GetDlgItem(editor_body, 101)));
    SetWindowTextW(GetDlgItem(editor_body, 300), L"Draft");
    const auto baseline = bytes(config / L"Simpilot.ini");
    simpilot::SettingsParticipantRegistry registry;
    auto registration = registry.add("quick_launch", 0, {
        .dirty = [&] { return ui->dirty(); }, .prepare = [&] { return ui->prepare(); },
        .apply = [&] { return ui->apply(); }, .write = [&](auto& doc) { settings.write(doc); },
        .rollback = [&] { return ui->rollback(); }, .finish = [&] { ui->finish(); }});
    simpilot::SettingsSession session(registry, {});
    require(!session.apply([&](const auto& document) { return document.save(root); }), "INI failure rolls back menu");
    require(bytes(config / L"Simpilot.ini") == baseline && session.dirty(),
        "Rollback preserves original menu and retryable UI draft");
    require(session.apply([&](const auto& doc) { return doc.save(config / L"Setting.ini"); }),
        "Retry commits menu and INI");
    require(bytes(config / L"Simpilot.ini").find("Draft") != std::string::npos && !session.dirty(),
        "Successful apply establishes new UI baseline");
    page.reset(); DestroyWindow(parent); session.cancel(); ui.reset();
}

void module_lifecycle(const std::filesystem::path& root) {
    const auto instance = GetModuleHandleW(nullptr);
    simpilot::Localization language("en-US");
    simpilot::ProgramSearchRegistry search;
    simpilot::TrayMenuRegistry tray;
    simpilot::UiDispatcher dispatcher(instance);
    simpilot::SettingsRegistry pages;
    simpilot::SettingsParticipantRegistry participants;
    simpilot::HotkeyRegistry hotkeys;
    simpilot::PopupMenuHost menus;
    auto document = simpilot::SettingsDocument::parse("[Other]\nUnknown=retained\n");
    auto module = simpilot::make_quick_launch_module(instance, root, document, language,
        search, tray, dispatcher, pages, participants, hotkeys, menus, [] { return false; }, {}, {});
    module->start();
    module->start();
    require(pages.size() == 2 && participants.size() == 1
        && tray.size() == 3 && tray.primary_actions.size() == 1,
        "Factory registers pages, configuration, tray and primary action");
    simpilot::SettingsSession session(participants, document);
    const auto drafts = hotkeys.editable_drafts();
    require(drafts.size() == 2, "Module owns both menu hotkeys");
    auto value = drafts[0].read();
    value.binding.gesture = simpilot::HotKeyGesture{MOD_CONTROL, VK_F24};
    drafts[0].write(value);
    require(!session.apply([](const auto&) { return false; }) && session.dirty(),
        "Module runtime rolls back on failed commit");
    require(session.apply([&](const auto& candidate) {
        document = candidate; return true;
    }) && !session.dirty(), "Module participant commits draft");
    require(document.get(L"MainMenuCode") == L"2,135" && document.get(L"Unknown") == L"retained",
        "Module writes owned keys without losing unrelated content");
    session.cancel();
    module->stop(); module->stop();
    require(!drafts[0].active() && pages.size() == 0 && participants.size() == 0
        && tray.size() == 0 && tray.primary_actions.size() == 0,
        "Stop revokes contributions and stops watcher");
}
}
int main() {
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        const auto dialogs = SetTimer(nullptr, 0, 20, dismiss_timer);
        codec();
        const auto root = std::filesystem::temp_directory_path()
            / (L"simpilot-quick-launch-module-" + std::to_wstring(GetCurrentProcessId()));
        transaction(root / L"menu_failure", 0);
        transaction(root / L"rollback_failure", 1);
        transaction(root / L"icon_failure", 2);
        ini_and_icon_failures(root / L"ini_failure");
        module_lifecycle(root / L"module");
        KillTimer(nullptr, dialogs);
        std::filesystem::remove_all(root);
        std::cout << "Quick launch settings transactions passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
