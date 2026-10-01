#include "settings_window.hpp"
#include "general_settings_page.hpp"
#include "about_window.hpp"
#include "hotkey_settings_page.hpp"
#include "keyboard_manager.hpp"
#include "everything_module.hpp"
#include "mouse_locator_module.hpp"
#include "cursor_locator.hpp"
#include "windows_hotkey_blocker_module.hpp"
#include "custom_hotkey_module.hpp"
#include "custom_hotkey_dialog.hpp"
#include "keyboard_mapping_module.hpp"
#include "keyboard_mapping_dialog.hpp"
#include "quick_launch_module.hpp"
#include "quick_launch_settings.hpp"
#include "menu_writer.hpp"
#include "menu_parser.hpp"
#include "program_selection_dialog.hpp"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <delayimp.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace simpilot;

namespace {
fs::path audit_root;
std::wstring last_action = L"Ready. All data is synthetic; execution and recording are disabled.";
unsigned blocked_actions = 0;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void blocked() {
    ++blocked_actions;
    last_action = L"Blocked external action (fixture safety boundary). Count: "
        + std::to_wstring(blocked_actions);
    SetLastError(ERROR_ACCESS_DENIED);
}

HINSTANCE WINAPI deny_shell(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT) {
    blocked();
    return reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED);
}

BOOL WINAPI deny_shell_ex(SHELLEXECUTEINFOW* info) {
    blocked();
    if (info) {
        info->hProcess = nullptr;
        info->hInstApp = reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED);
    }
    return FALSE;
}

FARPROC WINAPI shell_boundary(unsigned notification, PDelayLoadInfo info) {
    if (notification != dliNotePreGetProcAddress || !info->dlp.fImportByName) return nullptr;
    if (_stricmp(info->szDll, "shell32.dll") != 0) return nullptr;
    if (std::strcmp(info->dlp.szProcName, "ShellExecuteW") == 0)
        return reinterpret_cast<FARPROC>(&deny_shell);
    if (std::strcmp(info->dlp.szProcName, "ShellExecuteExW") == 0)
        return reinterpret_cast<FARPROC>(&deny_shell_ex);
    return nullptr;
}

fs::path executable_path() {
    std::wstring value(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    require(count != 0 && count < value.size(), "Cannot locate fixture executable");
    value.resize(count);
    return value;
}

fs::path unique_root() {
    GUID id{};
    require(SUCCEEDED(CoCreateGuid(&id)), "Cannot create unique fixture ID");
    wchar_t text[40]{};
    StringFromGUID2(id, text, 40);
    const auto root = fs::temp_directory_path() / (std::wstring(L"Simpilot-native-audit-") + text);
    require(fs::create_directory(root), "Cannot create isolated temp directory");
    return root;
}

PhysicalKey key(UINT vk) {
    return {vk, MapVirtualKeyW(vk, MAPVK_VK_TO_VSC), false};
}

struct Samples {
    SettingsDocument document;
    CustomHotkeySettings custom;
    KeyboardMappingSettings mapping;
    std::vector<ProgramCandidate> candidates;
};

Samples seed(const fs::path& root, int count, const std::string& language) {
    fs::create_directories(root / L"Config");
    fs::create_directories(root / L"Cache" / L"RunIcon");
    const auto files = root / L"Mock files" / L"Long project name for native column overflow review";
    fs::create_directories(files);
    Samples samples;
    AppSettings general;
    general.language = Localization::language_from_code(language);
    general.open_settings = {{HotKeyGesture{MOD_CONTROL | MOD_ALT, L'S'}, false}, true};
    AppSettingsStore::write(samples.document, general);
    QuickLaunchSettings quick;
    quick.main_menu = {{HotKeyGesture{MOD_ALT, VK_SPACE}, false}, true};
    quick.second_menu = {{HotKeyGesture{MOD_CONTROL, VK_SPACE}, false}, true};
    quick.write(samples.document);
    WindowsHotkeyBlockingSettings blocking;
    for (const auto index : {0, 2, 5, 6, 22}) blocking.disabled[index] = count != 0;
    blocking.write(samples.document);
    MouseLocatorSettings{false}.write(samples.document);

    MenuDocument main, second;
    main.root = std::make_unique<MenuCategory>(L"Main");
    second.root = std::make_unique<MenuCategory>(L"Second");
    auto tools = std::make_unique<MenuCategory>(L"Tools / \u5de5\u5177");
    tools->access_key = L'T';
    auto nested = std::make_unique<MenuCategory>(L"Review utilities / \u5ba1\u6838\u5de5\u5177");
    auto documents = std::make_unique<MenuCategory>(L"Documents / \u6587\u6863");
    const auto mock_exe = files / L"AuditTool.exe";
    fs::copy_file(executable_path(), mock_exe);
    for (int i = 0; i < std::max(count, 4); ++i) {
        const auto path = files / (L"Sample document " + std::to_wstring(i + 1)
            + L" - long descriptive target for truncation review.txt");
        SettingsDocument payload;
        payload.set(L"Fixture", L"Notice", L"Synthetic audit input. No personal configuration.");
        require(payload.save(path), "Cannot write safe sample file");
        if (i < 4) {
            const auto candidate_dir = root / L"Mock candidates" / (L"Release " + std::to_wstring(i + 1));
            fs::create_directories(candidate_dir);
            const auto candidate = candidate_dir / L"AuditTool.exe";
            fs::copy_file(mock_exe, candidate);
            samples.candidates.push_back({candidate,
                (std::uint64_t{1} << 48) | (std::uint64_t{static_cast<unsigned>(i + 1)} << 32),
                fs::last_write_time(candidate)});
        }
        if (i >= count) continue;
        CustomGlobalHotKey hotkey;
        hotkey.binding = {HotKeyGesture{MOD_CONTROL | MOD_ALT,
            static_cast<UINT>(VK_F1 + i % 12)}, false};
        if (i >= 12) hotkey.binding.gesture->modifiers |= MOD_SHIFT;
        if (i >= 24) hotkey.binding.gesture->modifiers = MOD_CONTROL | MOD_SHIFT;
        if (i >= 36) hotkey.binding.gesture->modifiers = MOD_ALT | MOD_SHIFT;
        hotkey.action = i % 3 == 0 ? CustomHotKeyAction::open_application
            : i % 3 == 1 ? CustomHotKeyAction::open_folder : CustomHotKeyAction::open_file;
        hotkey.program_path = (i % 3 == 0 ? mock_exe : i % 3 == 1 ? files : path).wstring();
        hotkey.working_directory = files.wstring();
        hotkey.arguments = i % 3 == 0 ? L"--audit-only \"Synthetic workspace\"" : L"";
        hotkey.enabled = i % 4 != 3;
        hotkey.run_as_administrator = i == 6;
        hotkey.existing_process_action = static_cast<ExistingProcessAction>(i % 3);
        hotkey.visibility = static_cast<LaunchVisibility>(i % 4);
        samples.custom.items.push_back(hotkey);

        KeyboardMappingRule rule;
        rule.trigger.modifiers[0] = key(VK_LCONTROL);
        rule.trigger.modifier_count = 1;
        // A second letter set with an extra modifier conflicts with the first
        // set under prefix validation. Use distinct action keys after Z.
        rule.trigger.action = key(static_cast<UINT>(
            i < 26 ? L'A' + i : VK_F1 + i - 26));
        rule.output.modifiers[0] = key(VK_LMENU);
        rule.output.modifier_count = 1;
        rule.output.action = key(static_cast<UINT>(VK_F1 + i % 12));
        rule.purpose = i % 3 == 0
            ? L"Long mapping purpose: review selection across multiple workspace panels / \u9009\u62e9\u5207\u6362"
            : L"Fixture mapping " + std::to_wstring(i + 1) + L" / \u6620\u5c04";
        rule.process_name = i % 2 == 0 ? L"audittool.exe" : L"";
        rule.exact_match = i % 3 != 0;
        rule.enabled = i % 5 != 4;
        samples.mapping.rules.push_back(rule);
        auto entry = std::make_unique<MenuEntry>(
            L"Document " + std::to_wstring(i + 1) + L" / \u6587\u6863 - detailed review",
            L"\"" + path.wstring() + L"\"", MenuEntryKind::command, 0);
        (i % 2 == 0 ? documents->children : nested->children).push_back(std::move(entry));
    }
    if (count != 0) {
        main.root->children.push_back(std::make_unique<MenuEntry>(
            L"Audit Tool / \u6a21\u62df\u5e94\u7528", L"\"" + mock_exe.wstring() + L"\"",
            MenuEntryKind::command, 0));
        for (const auto access_key : {L'H', L'O'}) {
            auto chrome = std::make_unique<MenuEntry>(L"Google Chrome",
                L"\"" + mock_exe.wstring() + L"\"" + (access_key == L'O' ? L" --incognito" : L""),
                MenuEntryKind::command, 0);
            chrome->access_key = access_key;
            main.root->children.push_back(std::move(chrome));
        }
        main.root->children.push_back(std::make_unique<MenuSeparator>());
        tools->children.push_back(std::make_unique<MenuEntry>(
            L"Fixture folder / \u6d4b\u8bd5\u6587\u4ef6\u5939", L"\"" + files.wstring() + L"\"",
            MenuEntryKind::command, 0));
        tools->children.push_back(std::move(nested));
        main.root->children.push_back(std::move(tools));
        second.root->children.push_back(std::make_unique<MenuEntry>(
            L"Second menu tool / \u7b2c\u4e8c\u83dc\u5355", L"\"" + mock_exe.wstring() + L"\"",
            MenuEntryKind::command, 0));
        second.root->children.push_back(std::make_unique<MenuSeparator>());
        second.root->children.push_back(std::move(documents));
    }
    samples.custom.write(samples.document);
    samples.mapping.write(samples.document);
    require(CustomHotkeySettings::read(samples.document) == samples.custom, "Hotkey codec round trip failed");
    require(KeyboardMappingSettings::read(samples.document) == samples.mapping, "Mapping codec round trip failed");
    require(validate_keyboard_mappings(samples.mapping.rules).empty(), "Invalid fixture mappings");
    require(samples.document.save(root / L"Setting.ini"), "Cannot save fixture settings");
    MenuWriter::save_file(root / L"Config" / L"Simpilot.ini", main);
    MenuWriter::save_file(root / L"Config" / L"Simpilot2.ini", second);
    require(MenuParser::parse_file(root / L"Config" / L"Simpilot.ini").entries().size()
        == main.entries().size(), "Menu writer/parser round trip failed");
    return samples;
}

// Modules and registrations die before their registries and unstarted keyboard.
struct Context {
    HINSTANCE instance;
    fs::path root;
    Samples samples;
    Localization localization;
    SettingsRegistry pages;
    SettingsParticipantRegistry participants;
    HotkeyRegistry hotkeys;
    ProgramSearchRegistry search;
    TrayMenuRegistry tray;
    UiDispatcher dispatcher;
    KeyboardManager keyboard;
    PopupMenuHost menus;
    AppSettings general, draft;
    std::vector<Registration> registrations;
    std::vector<std::unique_ptr<IAppModule>> modules;

    Context(HINSTANCE h, fs::path directory, int count, const std::string& language)
        : instance(h), root(std::move(directory)), samples(seed(root, count, language)),
          localization(language), dispatcher(h),
          general(AppSettingsStore::load(root / L"Setting.ini")), draft(general) {
        const auto diagnostic = [](std::wstring_view text) { last_action = std::wstring(text); };
        modules.push_back(make_quick_launch_module(h, root, samples.document, localization,
            search, tray, dispatcher, pages, participants, hotkeys, menus,
            [] { return true; }, [] {}, diagnostic));
        modules.push_back(make_everything_module(root, localization, search, tray, dispatcher,
            hotkeys, pages, participants, samples.document, diagnostic));
        modules.push_back(make_mouse_locator_module(h, samples.document, pages, participants, diagnostic));
        modules.push_back(make_windows_hotkey_blocker_module(samples.document, pages,
            participants, hotkeys, [](const auto&) { return true; }));
        modules.push_back(make_custom_hotkey_module(samples.document, root, participants,
            hotkeys, keyboard, {}, diagnostic));
        modules.push_back(make_keyboard_mapping_module(samples.document, pages, participants,
            keyboard, diagnostic));
        for (const auto& module : modules) module->start();
        registrations.push_back(participants.add("audit.general", 10, {
            .begin = [this] { draft = general; },
            .dirty = [this] { return draft != general; },
            .write = [this](SettingsDocument& document) { AppSettingsStore::write(document, draft); },
            .finish = [this] { general = draft; },
            .cancel = [this] { draft = general; }}));
        registrations.push_back(hotkeys.add("audit.settings", 30, {
            "settings.open_settings", [this] { return general.open_settings; },
            [](HWND) {}, [this] { return &draft.open_settings; }}));
        registrations.push_back(pages.add("general", 10, {"settings.tab.general",
            [this] { return make_general_settings_page(draft); }}));
        registrations.push_back(pages.add("hotkeys", 40, {"settings.tab.global_hotkeys",
            [this, diagnostic] {
                return make_hotkey_settings_page(hotkeys, keyboard, {}, diagnostic);
            }}));
        require(pages.size() == 8, "Expected eight real settings pages");
        require(!keyboard.running(), "Keyboard manager must remain unstarted");
    }

    void menu(bool second, HWND owner) {
        hotkeys.visit([&](const auto& id, const auto& item) {
            if (id == (second ? "quick_launch.second" : "quick_launch.main")) item.invoke(owner);
        });
    }
};

enum : int {
    language_zh = 100, language_en, commit_ok, commit_fail,
    settings_full, settings_empty, settings_few, settings_many,
    custom_add, custom_edit, mapping_add, mapping_edit, program_select,
    about, main_menu, second_menu, tray_menu, locator_open,
    theme_system, theme_light, theme_dark
};

class Launcher {
public:
    explicit Launcher(HINSTANCE instance) : instance_(instance), locator_(instance) {}
    ~Launcher() {
        context_.reset();
        if (font_) DeleteObject(font_);
    }

    int run() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = procedure;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"Simpilot.NativeAuditLauncher";
        require(RegisterClassW(&wc) != 0, "Cannot register launcher");
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName,
            L"Simpilot native audit fixture - synthetic data / no execution",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
            MulDiv(880, GetDpiForSystem(), 96), MulDiv(760, GetDpiForSystem(), 96),
            nullptr, nullptr, instance_, this);
        require(window_ != nullptr, "Cannot create launcher");
        ShowWindow(window_, SW_SHOW);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return 0;
    }

private:
    void controls() {
        const std::array<std::pair<int, const wchar_t*>, 21> definitions{{
            {language_zh, L"zh-CN"}, {language_en, L"en-US"},
            {commit_ok, L"Commit: SUCCESS (fake)"}, {commit_fail, L"Commit: FAILURE (fake)"},
            {settings_full, L"Settings: full (16 rows)"}, {settings_empty, L"Settings: empty (0 rows)"},
            {settings_few, L"Settings: few (3 rows)"}, {settings_many, L"Settings: many (40 rows)"},
            {custom_add, L"Custom hotkey: add"}, {custom_edit, L"Custom hotkey: edit"},
            {mapping_add, L"Keyboard mapping: add"}, {mapping_edit, L"Keyboard mapping: edit"},
            {program_select, L"Program selection (4 mock apps)"}, {about, L"About (links blocked)"},
            {main_menu, L"Main launch menu"}, {second_menu, L"Second launch menu"},
            {tray_menu, L"Tray popup (RECREATED host)"}, {locator_open, L"Cursor locator background"},
            {theme_system, L"Menu theme: system"}, {theme_light, L"Menu theme: light"},
            {theme_dark, L"Menu theme: dark"}
        }};
        for (const auto& [id, label] : definitions) {
            const auto control = CreateWindowW(L"BUTTON", label,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
            require(control != nullptr, "Cannot create launcher button");
            buttons_.push_back(control);
        }
        metadata_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 1, 1, window_, nullptr, instance_, nullptr);
        require(metadata_ != nullptr, "Cannot create metadata label");
        SetTimer(window_, 1, 1000, nullptr);
        layout();
        metadata();
    }

    void layout() {
        if (!metadata_) return;
        const auto dpi = GetDpiForWindow(window_);
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        const auto next_font = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, L"Segoe UI");
        RECT client{};
        GetClientRect(window_, &client);
        const int margin = scale(16), gap = scale(8), height = scale(36);
        const int columns = client.right < scale(640) ? 2 : 3;
        const int width = std::max(1L, (client.right - 2 * margin - (columns - 1) * gap) / columns);
        for (std::size_t i = 0; i < buttons_.size(); ++i) {
            MoveWindow(buttons_[i], margin + static_cast<int>(i % columns) * (width + gap),
                margin + static_cast<int>(i / columns) * (height + gap), width, height, TRUE);
            SendMessageW(buttons_[i], WM_SETFONT, reinterpret_cast<WPARAM>(next_font), TRUE);
        }
        const int top = margin + static_cast<int>((buttons_.size() + columns - 1) / columns) * (height + gap);
        MoveWindow(metadata_, margin, top, std::max(1L, client.right - 2 * margin),
            std::max(scale(50), static_cast<int>(client.bottom) - top - margin), TRUE);
        SendMessageW(metadata_, WM_SETFONT, reinterpret_cast<WPARAM>(next_font), TRUE);
        if (font_) DeleteObject(font_);
        font_ = next_font;
    }

    void fresh(int count) {
        context_.reset();
        current_count_ = count;
        const auto root = audit_root / (L"Variant-" + std::to_wstring(++generation_)
            + L"-rows-" + std::to_wstring(count));
        context_ = std::make_unique<Context>(instance_, root, count, language_);
        auto quick = QuickLaunchSettings::read(context_->samples.document);
        quick.menu_theme = theme_;
        // Menus need the selected theme at module construction, not only in the INI.
        if (theme_ != MenuTheme::system) {
            quick.write(context_->samples.document);
            require(context_->samples.document.save(root / L"Setting.ini"), "Cannot save theme fixture");
            context_->modules.front()->stop();
            context_->modules.front() = make_quick_launch_module(instance_, root,
                context_->samples.document, context_->localization, context_->search,
                context_->tray, context_->dispatcher, context_->pages, context_->participants,
                context_->hotkeys, context_->menus, [] { return true; }, [] {}, {});
            context_->modules.front()->start();
        }
        metadata();
    }

    void settings(int count) {
        fresh(count);
        last_action = L"Settings open: change a real control, then Apply/Save to exercise the selected fake commit.";
        (void)SettingsWindow::show_modal(instance_, window_, language_,
            context_->root / L"Setting.ini", context_->pages, context_->participants,
            [this](const SettingsDocument& candidate) {
                last_action = commit_success_ ? L"Fake commit returned SUCCESS; no personal settings saved."
                    : L"Fake commit returned FAILURE; real SettingsSession rollback executed.";
                if (commit_success_)
                    require(candidate.save(context_->root / L"last-successful-candidate.ini"),
                        "Cannot save candidate in fixture temp directory");
                metadata();
                return commit_success_;
            }, [this](std::string language) {
                language_ = language;
                context_->localization.set_language(language);
                context_->draft.language = Localization::language_from_code(language);
                context_->draft.language_code = context_->draft.language == UiLanguage::external ? language : "";
                return true;
            }, [](std::wstring_view text) { last_action = std::wstring(text); });
    }

    void tray() {
        const auto popup = CreatePopupMenu();
        const auto& loc = context_->localization;
        AppendMenuW(popup, MF_STRING, 1, loc.text("ui.settings").data());
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        const auto languages = CreatePopupMenu();
        for (const auto& [id, key_name] : std::array<std::pair<UINT, const char*>, 3>{{
            {2, "ui.language.simplified_chinese"}, {3, "ui.language.traditional_chinese"},
            {4, "ui.language.english"}}})
            AppendMenuW(languages, MF_STRING, id, loc.text(key_name).data());
        CheckMenuRadioItem(languages, 2, 4, language_ == "en-US" ? 4 : language_ == "zh-TW" ? 3 : 2, MF_BYCOMMAND);
        AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(languages), loc.text("ui.language").data());
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        std::unordered_map<std::string, HMENU> groups;
        UINT id = 1000;
        context_->tray.visit([&](const auto&, const TrayCommand& command) {
            auto parent = popup;
            if (!command.group.empty()) {
                auto [it, inserted] = groups.try_emplace(command.group, nullptr);
                if (inserted) {
                    it->second = CreatePopupMenu();
                    AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(it->second),
                        loc.text(command.group).data());
                }
                parent = it->second;
            }
            AppendMenuW(parent, !command.enabled || command.enabled() ? MF_STRING : MF_GRAYED,
                id++, loc.text(command.label_key).data());
        });
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(popup, MF_STRING, 5, loc.text("ui.about").data());
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(popup, MF_STRING, 6, loc.text("ui.exit").data());
        POINT point{};
        GetCursorPos(&point);
        const auto selected = context_->menus.track(window_, popup, TPM_LEFTALIGN | TPM_BOTTOMALIGN, point);
        if (selected) last_action = L"Recreated tray host: selection observed only; no command dispatched.";
    }

    static LRESULT CALLBACK background_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE)
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
        auto* self = reinterpret_cast<Launcher*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            const auto dc = BeginPaint(window, &paint);
            RECT bounds{};
            GetClientRect(window, &bounds);
            const auto middle = bounds.right / 2;
            RECT left{0, 0, middle, bounds.bottom}, right{middle, 0, bounds.right, bounds.bottom};
            const auto light = CreateSolidBrush(RGB(242, 244, 246));
            const auto dark = CreateSolidBrush(RGB(46, 48, 50));
            FillRect(dc, &left, light);
            FillRect(dc, &right, dark);
            DeleteObject(light);
            DeleteObject(dark);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(35, 35, 35));
            DrawTextW(dc, L"Controlled light background", -1, &left, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(dc, RGB(240, 240, 240));
            DrawTextW(dc, L"Controlled dark background", -1, &right, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(window, &paint);
            return 0;
        }
        if (message == WM_DESTROY && self) {
            (void)self->locator_.set_enabled(false);
            self->background_ = nullptr;
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    void background() {
        if (background_) { SetForegroundWindow(background_); return; }
        WNDCLASSW wc{};
        wc.hInstance = instance_;
        wc.lpfnWndProc = background_procedure;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"Simpilot.AuditLocatorBackground";
        if (!RegisterClassW(&wc)) require(GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "Background registration failed");
        background_ = CreateWindowW(wc.lpszClassName,
            L"Real cursor locator - manually shake pointer; close to stop (no synthetic input)",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 650,
            window_, nullptr, instance_, this);
        require(background_ != nullptr, "Cannot create controlled background");
        require(locator_.set_enabled(true), "Real cursor locator could not start");
        ShowWindow(background_, SW_SHOW);
    }

    void command(int id) {
        switch (id) {
        case language_zh: language_ = "zh-CN"; context_.reset(); break;
        case language_en: language_ = "en-US"; context_.reset(); break;
        case commit_ok: commit_success_ = true; break;
        case commit_fail: commit_success_ = false; break;
        case theme_system: case theme_light: case theme_dark:
            theme_ = static_cast<MenuTheme>(id - theme_system); context_.reset(); break;
        case settings_full: settings(16); break;
        case settings_empty: settings(0); break;
        case settings_few: settings(3); break;
        case settings_many: settings(40); break;
        case locator_open: background(); break;
        default:
            if (!context_ || context_->samples.custom.items.empty()) fresh(16);
            if (id == custom_add || id == custom_edit) {
                const auto result = CustomHotKeyDialog::show_modal(instance_, window_, language_,
                    context_->keyboard, context_->root, id == custom_edit ? &context_->samples.custom.items.front() : nullptr);
                last_action = result ? L"Real hotkey dialog accepted; returned draft discarded."
                    : L"Hotkey dialog closed. Recording is unsupported (keyboard thread is not started).";
            } else if (id == mapping_add || id == mapping_edit) {
                const auto result = KeyboardMappingDialog::show_modal(instance_, window_, language_,
                    id == mapping_edit ? &context_->samples.mapping.rules.front() : nullptr);
                last_action = result ? L"Real mapping dialog accepted; returned draft discarded."
                    : L"Mapping dialog closed; capture callbacks and foreground-process lookup are absent.";
            } else if (id == program_select) {
                const auto result = ProgramSelectionDialog::show_modal(instance_, window_, language_,
                    L"AuditTool.exe", context_->samples.candidates);
                last_action = result ? L"Selected mock path: " + result->wstring() : L"Program selection cancelled.";
            } else if (id == about) {
                AboutWindow::show_modal(instance_, window_, context_->localization,
                    context_->root / L"Mock files" / L"AuditTool.exe", L"1.0.0 - AUDIT FIXTURE (simulated)");
            } else if (id == main_menu || id == second_menu) context_->menu(id == second_menu, window_);
            else if (id == tray_menu) tray();
            break;
        }
        metadata();
    }

    void metadata() {
        if (!metadata_) return;
        const auto single_line = [](std::wstring_view value) {
            std::wstring result;
            for (const auto character : value) {
                if (character == L'\r') result.append(L"\\r");
                else if (character == L'\n') result.append(L"\\n");
                else result.push_back(character);
            }
            return result;
        };
        SettingsDocument data;
        data.set(L"Audit", L"Simulation", L"Synthetic data; real production UI except recreated tray host");
        data.set(L"Audit", L"TempRoot", audit_root.wstring());
        data.set(L"Audit", L"Language", std::wstring(language_.begin(), language_.end()));
        data.set(L"Audit", L"Commit", commit_success_ ? L"fake success" : L"fake failure");
        data.set(L"Audit", L"Rows", std::to_wstring(current_count_));
        data.set(L"Audit", L"MenuTheme", std::to_wstring(static_cast<int>(theme_)));
        data.set(L"Audit", L"PID", std::to_wstring(GetCurrentProcessId()));
        data.set(L"Audit", L"SystemDPI", std::to_wstring(GetDpiForSystem()));
        data.set(L"Audit", L"Build", L"MSVC x64 Release; existing static libraries; PMv2 manifest");
        const std::string built = AUDIT_CONFIGURED;
        data.set(L"Audit", L"ConfiguredUTC", std::wstring(built.begin(), built.end()));
        data.set(L"Audit", L"Executable", executable_path().wstring());
        data.set(L"Audit", L"VariantRoot", context_ ? context_->root.wstring() : L"(not created yet)");
        data.set(L"Audit", L"LastAction", single_line(last_action));
        struct Windows {
            SettingsDocument* data;
            decltype(single_line)* encode;
            int index = 0;
        } windows{&data, &single_line};
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM parameter) -> BOOL {
            if (!IsWindowVisible(window)) return TRUE;
            auto& state = *reinterpret_cast<Windows*>(parameter);
            wchar_t title[512]{}, type[128]{};
            GetWindowTextW(window, title, 512);
            GetClassNameW(window, type, 128);
            RECT rect{}, client{};
            GetWindowRect(window, &rect);
            GetClientRect(window, &client);
            const auto prefix = L"Window" + std::to_wstring(++state.index);
            state.data->set(L"Windows", prefix + L"Title", (*state.encode)(title));
            state.data->set(L"Windows", prefix + L"Class", type);
            state.data->set(L"Windows", prefix + L"DPI", std::to_wstring(GetDpiForWindow(window)));
            state.data->set(L"Windows", prefix + L"Rect", std::to_wstring(rect.left) + L","
                + std::to_wstring(rect.top) + L"," + std::to_wstring(rect.right - rect.left)
                + L"," + std::to_wstring(rect.bottom - rect.top));
            state.data->set(L"Windows", prefix + L"Client", std::to_wstring(client.right) + L"x"
                + std::to_wstring(client.bottom));
            return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));
        (void)data.save(audit_root / L"metadata.ini");
        const auto language = std::wstring(language_.begin(), language_.end());
        const auto display = L"SIMULATED DATA | " + language
            + L" | Commit: " + (commit_success_ ? std::wstring(L"SUCCESS") : std::wstring(L"FAILURE"))
            + L" | Rows: " + std::to_wstring(current_count_)
            + L" | Launcher DPI: " + std::to_wstring(GetDpiForWindow(window_))
            + L"\r\nBuild: MSVC x64 Release, real app sources + existing static libs"
            + L"\r\nTemp data / live window metadata: " + audit_root.wstring()
            + L"\r\nGAPS: tray host recreated; keyboard recording unavailable; Everything absent;"
            + L" About version simulated; external actions blocked."
            + L"\r\n" + last_action;
        if (display != previous_metadata_) {
            SetWindowTextW(metadata_, display.c_str());
            previous_metadata_ = display;
        }
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            auto* self = static_cast<Launcher*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto* self = reinterpret_cast<Launcher*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return DefWindowProcW(window, message, wparam, lparam);
        try {
            switch (message) {
            case WM_CREATE: self->controls(); return 0;
            case WM_SIZE: self->layout(); return 0;
            case WM_GETMINMAXINFO:
                reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {
                    MulDiv(580, GetDpiForWindow(window), 96), MulDiv(700, GetDpiForWindow(window), 96)};
                return 0;
            case WM_DPICHANGED: {
                const auto* bounds = reinterpret_cast<RECT*>(lparam);
                SetWindowPos(window, nullptr, bounds->left, bounds->top,
                    bounds->right - bounds->left, bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
                self->layout();
                return 0;
            }
            case WM_TIMER: self->metadata(); return 0;
            case WM_COMMAND:
                if (HIWORD(wparam) == BN_CLICKED) self->command(LOWORD(wparam));
                return 0;
            case WM_DESTROY:
                KillTimer(window, 1);
                (void)self->locator_.set_enabled(false);
                PostQuitMessage(0);
                return 0;
            }
        } catch (const std::exception& error) {
            const std::string text = error.what();
            MessageBoxW(window, std::wstring(text.begin(), text.end()).c_str(),
                L"Audit fixture error", MB_OK | MB_ICONERROR);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    HINSTANCE instance_;
    HWND window_ = nullptr, metadata_ = nullptr, background_ = nullptr;
    HFONT font_ = nullptr;
    std::vector<HWND> buttons_;
    std::unique_ptr<Context> context_;
    CursorLocator locator_;
    std::string language_ = "zh-CN";
    std::wstring previous_metadata_;
    bool commit_success_ = true;
    int generation_ = 0, current_count_ = 16;
    MenuTheme theme_ = MenuTheme::system;
};

int self_test(HINSTANCE instance) {
    SettingsDocument report;
    for (const auto count : {0, 3, 16, 40}) {
        Context context(instance, audit_root / (L"self-test-" + std::to_wstring(count)), count, "en-US");
        require(context.samples.custom.items.size() == static_cast<std::size_t>(count), "Hotkey row count");
        require(context.samples.mapping.rules.size() == static_cast<std::size_t>(count), "Mapping row count");
        require(context.pages.size() == 8, "Page count");
        require(!fs::exists(context.root / L"Everything64.dll"), "Everything must be absent");
        SettingsSession session(context.participants, context.samples.document);
        require(!session.apply([](const auto&) { return false; }), "Failure callback must fail");
        require(session.apply([](const auto&) { return true; }), "Success callback must succeed");
        require(!context.keyboard.running(), "Keyboard thread unexpectedly started");
        report.set(L"SelfTest", L"Rows" + std::to_wstring(count), L"PASS: codecs, pages, module lifecycle, rollback/commit");
    }
    // Exercise lazy resolution itself; eager lookup is case-sensitive to the
    // linker's DLL spelling and is not the execution boundary being tested.
    require(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"audit-denied", nullptr, nullptr, SW_HIDE))
        == SE_ERR_ACCESSDENIED, "ShellExecute boundary failed");
    SHELLEXECUTEINFOW execution{sizeof(execution)};
    execution.lpFile = L"audit-denied";
    require(!ShellExecuteExW(&execution) && GetLastError() == ERROR_ACCESS_DENIED,
        "ShellExecuteEx boundary failed");
    require(blocked_actions == 2, "Shell callbacks were not intercepted");
    report.set(L"SelfTest", L"ShellBoundary", L"PASS: ShellExecuteW and ShellExecuteExW denied");
    report.set(L"SelfTest", L"UIAudit", L"NOT RUN: no UI automation or screenshots");
    require(report.save(audit_root / L"self-test-results.ini"), "Cannot save self-test report");
    return 0;
}
} // namespace

// No command-execution library is linked. These two boundary functions are
// fixture implementations, not copies of production execution logic.
namespace simpilot {
void execute_command(HWND, const LaunchRequest&, const LaunchErrorSink&) { blocked(); }
std::wstring user_profile_directory() { return audit_root.wstring(); }
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = shell_boundary;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool testing = std::wstring_view(arguments).find(L"--self-test") != std::wstring_view::npos;
    int result = 1;
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls),
            ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LINK_CLASS};
        require(InitCommonControlsEx(&controls) != FALSE, "Cannot initialize common controls");
        audit_root = unique_root();
        result = testing ? self_test(instance) : Launcher(instance).run();
    } catch (const std::exception& error) {
        const std::string text = error.what();
        const std::wstring wide(text.begin(), text.end());
        if (!audit_root.empty()) {
            SettingsDocument failure;
            failure.set(L"Failure", L"Message", wide);
            (void)failure.save(audit_root / L"failure.ini");
        }
        if (!testing) MessageBoxW(nullptr, wide.c_str(), L"Audit fixture error", MB_OK | MB_ICONERROR);
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
