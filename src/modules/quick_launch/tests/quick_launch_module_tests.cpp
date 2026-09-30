#include "quick_launch_module.hpp"
#include "quick_launch_settings_ui.hpp"
#include "simpilot/config_file.hpp"
#include "simpilot/command.hpp"
#include <commctrl.h>
#include <fstream>
#include <iostream>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
HWND control(HWND parent, int identifier) {
    if (const auto child = GetDlgItem(parent, identifier)) return child;
    for (auto child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        if (const auto found = control(child, identifier)) return found;
    return nullptr;
}
RECT bounds(HWND window, HWND parent) {
    RECT rectangle{};
    GetWindowRect(window, &rectangle);
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&rectangle), 2);
    return rectangle;
}
bool has_style(HWND window, LONG_PTR style) {
    return (GetWindowLongPtrW(window, GWL_STYLE) & style) != 0;
}
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
    const auto tree = control(editor, 101);
    TreeView_SelectItem(tree, TreeView_GetRoot(tree));
    SetWindowTextW(control(editor, 300), L"Edited");
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
    const auto tree = control(editor, 101), name = control(editor, 300);
    require(tree && name, "Locate editor fields independently of scroll container depth");
    TreeView_SelectItem(tree, TreeView_GetRoot(tree));
    SetWindowTextW(name, L"Draft");
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

void editor_scrolling(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    std::wstring source = L"-Tools\nPrimary|notepad.exe\nAlias|notepad.exe\n-\n"
        L"Website|https://example.test\n";
    for (int index = 0; index < 30; ++index)
        source += L"Other " + std::to_wstring(index) + L"|notepad.exe\n";
    simpilot::write_configuration_text(root / L"Simpilot.ini", source);
    const auto instance = GetModuleHandleW(nullptr);
    const auto parent = CreateWindowW(L"STATIC", L"Editor scrolling test", WS_OVERLAPPEDWINDOW,
        -30000, -30000, 1100, 900, nullptr, nullptr, instance, nullptr);
    {
        simpilot::MenuEditorWindow editor(instance, parent, "en-US",
            root / L"Simpilot.ini", root / L"Simpilot2.ini", {}, {},
            [](auto) { return simpilot::MenuEditorWindow::ProgramResolutionInfo{
                std::filesystem::path(L"C:\\Windows\\notepad.exe"), true}; },
            [](HWND, auto) { return std::optional<std::filesystem::path>(
                L"C:\\Windows\\notepad.exe"); });
        require(editor.create(), "Create the real editor for scroll regression tests");
        const auto window = FindWindowExW(parent, nullptr, L"Simpilot.MenuEditorWindow", nullptr);
        const auto tree = control(window, 101), pane = control(window, 309);
        const auto viewport = GetWindow(pane, GW_CHILD);
        const auto content = GetWindow(viewport, GW_CHILD);
        const auto name = control(window, 300), access = control(window, 301);
        const auto arguments = control(window, 305), administrator = control(window, 306);
        require(GetParent(tree) == window && GetParent(control(window, 100)) == window
            && GetParent(control(window, 205)) == window && IsChild(content, name),
            "Menu navigation, tree and structure toolbar remain outside the one detail viewport");
        const auto category = TreeView_GetRoot(tree);
        const auto application = TreeView_GetChild(tree, category);
        const auto alias = TreeView_GetNextSibling(tree, application);
        const auto separator = TreeView_GetNextSibling(tree, alias);
        const auto website = TreeView_GetNextSibling(tree, separator);
        for (const auto dpi : {96u, 144u, 192u}) {
            const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
            editor.set_dpi(dpi);
            editor.set_bounds(0, 0, scale(780), scale(400));
            TreeView_SelectItem(tree, application);
            require(!has_style(window, WS_VSCROLL | WS_HSCROLL)
                && has_style(viewport, WS_VSCROLL) && !has_style(viewport, WS_HSCROLL),
                "Only long right-hand details scroll, with no redundant horizontal scrollbar");
            const auto resolution = bounds(control(window, 308), content);
            require(resolution.bottom - resolution.top == scale(32)
                && bounds(arguments, content).top > resolution.bottom,
                "Resolved-path row is laid out even while the settings host is hidden");
            const auto tree_before = bounds(tree, window);
            const auto segment_before = bounds(control(window, 100), window);
            const auto toolbar_before = bounds(control(window, 205), window);
            SendMessageW(viewport, WM_VSCROLL, SB_BOTTOM, 0);
            require(GetScrollPos(viewport, SB_VERT) > 0, "Details can be scrolled to the last field");
            const auto tree_after = bounds(tree, window);
            const auto segment_after = bounds(control(window, 100), window);
            const auto toolbar_after = bounds(control(window, 205), window);
            require(EqualRect(&tree_before, &tree_after) && EqualRect(&segment_before, &segment_after)
                && EqualRect(&toolbar_before, &toolbar_after), "Scrolling details never moves the menu tree or toolbar");
            TreeView_SelectItem(tree, alias);
            require(GetScrollPos(viewport, SB_VERT) == 0, "Selecting another menu item resets details to the top");
            SendMessageW(viewport, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
            require(GetScrollPos(viewport, SB_VERT) > 0, "Mouse wheel scrolls the detail viewport");
            TreeView_SelectItem(tree, category);
            require(!has_style(viewport, WS_VSCROLL | WS_HSCROLL)
                && GetScrollPos(viewport, SB_VERT) == 0,
                "Category fields shrink to their actual extent and remove stale scrollbars");
            TreeView_SelectItem(tree, separator);
            require(!has_style(viewport, WS_VSCROLL | WS_HSCROLL) && !has_style(name, WS_VISIBLE),
                "Separators have no hidden field extent or scrollbars");
            TreeView_SelectItem(tree, website);
            require(!has_style(viewport, WS_VSCROLL | WS_HSCROLL)
                && !has_style(arguments, WS_VISIBLE) && !has_style(administrator, WS_VISIBLE),
                "Web items do not reserve space for application-only details");
            editor.set_bounds(0, 0, scale(780), scale(700));
            TreeView_SelectItem(tree, application);
            require(!has_style(viewport, WS_VSCROLL | WS_HSCROLL),
                "Growing the editor removes unneeded detail scrollbars");
            editor.set_bounds(0, 0, scale(664), scale(400));
            require(!has_style(viewport, WS_HSCROLL)
                && bounds(pane, window).right <= scale(664), "Minimum supported width keeps details inside the editor");
        }
        ShowWindow(parent, SW_SHOWNOACTIVATE);
        editor.set_visible(true);
        editor.set_dpi(96);
        editor.set_bounds(0, 0, 780, 400);
        TreeView_SelectItem(tree, application);
        SetFocus(administrator);
        const auto checkbox_bounds = bounds(administrator, viewport);
        RECT viewport_bounds{};
        GetClientRect(viewport, &viewport_bounds);
        require(checkbox_bounds.top >= 0 && checkbox_bounds.bottom <= viewport_bounds.bottom,
            "Keyboard focus reveals a clipped bottom field inside the right viewport");
        SetFocus(name);
        require(GetNextDlgTabItem(window, name, FALSE) == access,
            "Reparenting preserves the detail fields' Tab order");
        require(GetScrollPos(viewport, SB_VERT) <= 28, "Focusing the first field scrolls back to its label");
    }
    DestroyWindow(parent);
}

void icon_target_names(const std::filesystem::path& root) {
    const auto instance = GetModuleHandleW(nullptr);
    const auto config = root / L"Config";
    std::filesystem::create_directories(config);
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto target = L"\"" + std::wstring(executable) + L"\"";
    simpilot::write_configuration_text(config / L"Simpilot.ini",
        L"Primary|" + target + L"\nAlternate|" + target + L"\n"
        L"Google Chrome(&H)|" + target + L"\nGoogle Chrome(&O)|" + target + L" --incognito\n");
    simpilot::write_configuration_text(config / L"Simpilot2.ini", L"Secondary|" + target + L"\n");
    simpilot::Localization language("en-US");
    simpilot::ProgramSearchRegistry search;
    simpilot::TrayMenuRegistry tray;
    simpilot::UiDispatcher dispatcher(instance);
    simpilot::SettingsRegistry pages;
    simpilot::SettingsParticipantRegistry participants;
    simpilot::HotkeyRegistry hotkeys;
    simpilot::PopupMenuHost menus;
    auto module = simpilot::make_quick_launch_module(instance, root, {}, language,
        search, tray, dispatcher, pages, participants, hotkeys, menus, [] { return false; }, {}, {});
    module->start();
    simpilot::SettingsSession session(participants, {});
    std::unique_ptr<simpilot::ISettingsPage> page;
    pages.visit([&](const auto& id, const auto& contribution) {
        if (id == "quick_launch.icons") page = contribution.create();
    });
    const auto parent = CreateWindowW(L"STATIC", L"Icon names test", WS_OVERLAPPEDWINDOW,
        0, 0, 1000, 700, nullptr, nullptr, instance, nullptr);
    const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    page->create({instance, parent, 96, font, language, [] {}});
    page->layout({0, 0, 780, 600}, 96, font);
    const auto list = control(parent, 500);
    require(ListView_GetItemCount(list) == 5,
        "Keep separate rows for Chrome(H) and Chrome(O) despite the same parsed name and application target");
    const std::array names{L"Primary", L"Alternate", L"Google Chrome(H)", L"Google Chrome(O)", L"Secondary"};
    for (int index = 0; index < static_cast<int>(names.size()); ++index) {
        wchar_t value[256]{};
        ListView_GetItemText(list, index, 1, value, 256);
        require(std::wstring_view(value) == names[index], "Icon index displays the configured menu name");
    }
    wchar_t normal_target[32768]{}, private_target[32768]{};
    ListView_GetItemText(list, 2, 2, normal_target, 32768);
    ListView_GetItemText(list, 3, 2, private_target, 32768);
    const auto normal_command = simpilot::ParsedCommand::try_parse(normal_target);
    const auto private_command = simpilot::ParsedCommand::try_parse(private_target);
    require(normal_command && private_command
        && normal_command->executable == executable && private_command->executable == executable
        && normal_command->arguments.empty() && private_command->arguments == L"--incognito",
        "Separately indexed Chrome labels retain their corresponding launch arguments");
    page.reset();
    DestroyWindow(parent);
    session.cancel();
    module->stop();
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
        editor_scrolling(root / L"editor_scrolling");
        icon_target_names(root / L"icon_names");
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
