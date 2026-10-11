#include "custom_hotkey_module.hpp"
#include "custom_hotkey_settings.hpp"
#include "custom_hotkey_dialog.hpp"
#include "hotkey_settings_page.hpp"
#include "keyboard_manager.hpp"
#include "toggle_switch.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <dlgs.h>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void codec_tests() {
    simpilot::CustomHotkeySettings settings;
#include "legacy_fixture.inc"
    auto document = simpilot::SettingsDocument::parse(
        "; retain\r\n[Other]\r\nUnknown=42\r\nCustomGlobalHotKey12Future=keep\r\n");
    settings.write(document);
    const auto content = document.serialize();
    require(content.find("CustomGlobalHotKey1Action=0") != std::string::npos
        && content.find("CustomGlobalHotKey2Action=2") != std::string::npos
        && content.find("CustomGlobalHotKey3Action=1") != std::string::npos, "Legacy action values");
    auto expected = settings;
    expected.items.pop_back();
    expected.items[0].binding.force_override = true;
    require(simpilot::CustomHotkeySettings::read(document) == expected,
        "Unicode actions, disabled state, process behavior and Win+letter compatibility");
    settings.items.resize(1);
    settings.write(document);
    require(!document.get(L"CustomGlobalHotKey2Program"), "Shortened collection removes owned old keys");
    require(document.get(L"CustomGlobalHotKey12Future") == L"keep" && document.get(L"Unknown") == L"42",
        "Do not remove unowned numbered fields or unrelated settings");
    document.set(L"General", L"Language", L"en-US");
    require(simpilot::CustomHotkeySettings::read(document).items.size() == 1,
        "Other modules can save while custom hotkeys are absent");
    const auto malformed = simpilot::SettingsDocument::parse(
        "[WrongSection]\ncustomglobalhotkeycount=1\nCUSTOMGLOBALHOTKEY1CODE=3,88\n"
        "CustomGlobalHotKey1Program=C:\\Apps\\Incomplete.exe\n");
    require(simpilot::CustomHotkeySettings::read(malformed).items.empty(), "Require explicit Enabled");
    auto legacy = malformed;
    legacy.set(L"Ignored", L"CustomGlobalHotKey1Enabled", L"0");
    require(simpilot::CustomHotkeySettings::read(legacy).items.size() == 1,
        "Sectionless case-insensitive legacy lookup");
}
void switch_image_tests() {
    for (const UINT dpi : {96U, 120U, 144U, 192U}) {
        const auto images = simpilot::toggle_switch::create_state_image_list(dpi);
        require(images && ImageList_GetImageCount(images) == 2, "Both switch states exist");
        const auto screen = GetDC(nullptr);
        const auto dc = CreateCompatibleDC(screen);
        const int width = MulDiv(64, dpi, 96), height = MulDiv(36, dpi, 96);
        const auto bitmap = CreateCompatibleBitmap(screen, width, height);
        const auto previous = SelectObject(dc, bitmap);
        const RECT bounds{0, 0, width, height};
        for (int state = 0; state < 2; ++state) {
            FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            require(ImageList_Draw(images, state, dc, 0, 0, ILD_TRANSPARENT),
                "Draw switch image");
            require(GetPixel(dc, 0, 0) == RGB(255, 255, 255),
                "Switch image corners must remain transparent");
            require(GetPixel(dc, width / 2, height / 2) != RGB(255, 0, 255),
                "Switch glyph must not contain the transparency key");
            if (state == 1) require(GetPixel(dc, width / 2, height / 2) == RGB(0, 103, 184),
                "Enabled switch renders its blue track, not a black rectangle");
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        ImageList_Destroy(images);
    }
}
void conditional_codec_tests() {
    using simpilot::ToggleRequirement;
    simpilot::CustomGlobalHotKey on;
    on.binding.gesture = simpilot::HotKeyGesture{MOD_CONTROL, VK_F24};
    on.program_path = L"C:\\Apps\\editor.exe";
    on.binding.activation.locks[1] = ToggleRequirement::on;
    auto off = on;
    off.binding.activation.locks[1] = ToggleRequirement::off;
    off.binding.activation.require_running_program = true;
    off.binding.activation.programs = {L"C:\\Apps\\后台.exe", L"C:\\Other\\editor.exe"};
    off.enabled = false;
    auto ordinary = on;
    ordinary.binding.gesture = simpilot::HotKeyGesture{MOD_ALT, VK_F23};
    ordinary.binding.activation = {};
    simpilot::CustomHotkeySettings settings{{on, ordinary, off}};
    auto document = simpilot::SettingsDocument::parse("[Unknown]\nKeep=value\nConditionalGlobalHotKey1Future=keep\n");
    settings.write(document);
    require(document.get(L"CustomGlobalHotKeyCount") == L"1"
        && document.get(L"CustomGlobalHotKey1Code") == L"1,134"
        && !document.get(L"CustomGlobalHotKey2Code"), "Old readers see only unconditional rules");
    require(document.get(L"ConditionalGlobalHotKeyCount") == L"2"
        && document.get(L"ConditionalGlobalHotKeyVersion") == L"1", "Independent versioned conditions");
    require(simpilot::CustomHotkeySettings::read(document) == settings, "Conditions, order and Unicode paths roundtrip");
    const auto bytes = document.serialize();
    simpilot::SettingsSnapshot snapshot;
    snapshot.files["Config/Setting.ini"] = std::vector<unsigned char>(bytes.begin(), bytes.end());
    std::vector<std::wstring> warnings;
    simpilot::inspect_custom_hotkey_backup(snapshot, true, warnings);
    require(std::ranges::find(warnings, L"C:\\Apps\\后台.exe") != warnings.end(),
        "Missing associated program warns without dropping the rule");
    const auto root = std::filesystem::temp_directory_path() /
        (L"Simpilot-conditional-backup-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root / L"Config");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code error; std::filesystem::remove_all(path, error);
    } } cleanup{root};
    auto original = simpilot::SettingsDocument::parse("[General]\nLanguage=en-US\nCustomGlobalHotKeyCount=0\n");
    require(original.save(root / L"Config/Setting.ini"), "Prepare isolated restoration target");
    simpilot::SettingsBackup::write(snapshot, root / L"conditions.simpilot-backup");
    const auto restored = simpilot::SettingsBackup::read(root / L"conditions.simpilot-backup");
    const auto validate = [](const auto& value) {
        std::vector<std::wstring> ignored;
        simpilot::inspect_custom_hotkey_backup(value, false, ignored);
    };
    simpilot::SettingsBackup::stage(root, restored, validate);
    require(simpilot::SettingsBackup::recover(root, validate), "Conditional configuration restores transactionally");
    require(simpilot::CustomHotkeySettings::read(simpilot::SettingsDocument::load(root / L"Config/Setting.ini")) == settings,
        "Restored rules reload with identical order, actions and conditions");
    require(!simpilot::SettingsBackup::recover(root, validate), "Subsequent startup does not reapply the restore");
    const auto rejected = [&](const wchar_t* key, const wchar_t* value) {
        auto bad = document;
        bad.set(L"CustomGlobalHotkeys", key, value);
        try { (void)simpilot::CustomHotkeySettings::read(bad); }
        catch (...) { return true; }
        return false;
    };
    require(rejected(L"ConditionalGlobalHotKeyVersion", L"2"), "Unknown version rejected");
    require(rejected(L"ConditionalGlobalHotKey1CapsLock", L"3"), "Invalid lock enum rejected");
    require(rejected(L"ConditionalGlobalHotKey1RequireRunning", L"1"), "Enabled empty program list rejected");
    require(rejected(L"ConditionalGlobalHotKey2RelatedProgram1", L"relative.exe"), "Invalid process path rejected");
    require(rejected(L"ConditionalGlobalHotKey2ProgramCount", L"33"), "Too many associated programs rejected");
    require(rejected(L"ConditionalGlobalHotKey2NumLock", L"1"), "Overlapping conditions rejected");
    require(rejected(L"ConditionalGlobalHotKey1Code", L"2,135oops"), "Malformed conditional gesture rejected");
    require(rejected(L"ConditionalGlobalHotKey2Order", L"0"), "Ambiguous combined ordering rejected");
    for (const auto field : {L"ConditionalGlobalHotKeyCount", L"ConditionalGlobalHotKeyVersion"}) {
        auto incomplete = document;
        incomplete.erase(field);
        bool incomplete_rejected = false;
        try { (void)simpilot::CustomHotkeySettings::read(incomplete); }
        catch (...) { incomplete_rejected = true; }
        require(incomplete_rejected, "Incomplete collection header cannot silently discard conditional rules");
    }
    auto missing = document;
    missing.erase(L"ConditionalGlobalHotKey1CapsLock");
    bool failed = false;
    try { (void)simpilot::CustomHotkeySettings::read(missing); } catch (...) { failed = true; }
    require(failed, "Missing condition fields cannot become unconditional");
    const auto malformed_bytes = missing.serialize();
    snapshot.files["Config/Setting.ini"] = std::vector<unsigned char>(malformed_bytes.begin(), malformed_bytes.end());
    failed = false;
    try { simpilot::inspect_custom_hotkey_backup(snapshot, false, warnings); } catch (...) { failed = true; }
    require(failed, "Backup preflight rejects malformed conditions");
    auto builtin_conflict = document;
    builtin_conflict.set(L"Hotkeys", L"OpenSettingsCode", L"2,135");
    builtin_conflict.set(L"Hotkeys", L"OpenSettingsEnabled", L"1");
    const auto conflict_bytes = builtin_conflict.serialize();
    snapshot.files["Config/Setting.ini"] = {conflict_bytes.begin(), conflict_bytes.end()};
    failed = false;
    try { simpilot::inspect_custom_hotkey_backup(snapshot, false, warnings); } catch (...) { failed = true; }
    require(failed, "Restore rejects condition conflicts with built-in hotkeys");
    settings.items = {ordinary};
    settings.write(document);
    require(!document.get(L"ConditionalGlobalHotKey2RelatedProgram1")
        && !document.get(L"ConditionalGlobalHotKey1Code"), "Removed rules do not leave active records");
    require(document.get(L"Keep") == L"value" && document.get(L"ConditionalGlobalHotKey1Future") == L"keep",
        "Unknown fields retained");
}
void scenario_codec_tests() {
    simpilot::CustomGlobalHotKey a;
    a.name = L"办公—搜索";
    a.program_path = L"C:\\Apps\\target.exe";
    a.binding.gesture = simpilot::HotKeyGesture{MOD_CONTROL, VK_F24};
    a.binding.activation.require_foreground_program = true;
    a.binding.activation.programs = {L"C:\\Apps\\editor.exe"};
    a.binding.activation.priority = 100;
    auto b = a;
    b.name = a.name; // Duplicate labels are descriptive, not rule identities.
    b.binding.activation.priority = 25;
    b.binding.activation.require_foreground_program = false;
    b.binding.activation.require_running_program = true;
    auto fallback = a;
    fallback.name.clear();
    fallback.binding.activation = {};
    fallback.binding.activation.priority = 0;
    simpilot::CustomHotkeySettings settings{{a, b, fallback}};
    require(settings.valid(), "Ranked foreground, background and default scenarios coexist");
    auto document = simpilot::SettingsDocument::parse("[Future]\nUnowned=yes\nConditionalGlobalHotKey1FutureField=keep\n");
    settings.write(document);
    require(document.get(L"ConditionalGlobalHotKeyVersion") == L"2"
        && document.get(L"CustomGlobalHotKeyCount") == L"0", "Scenario actions never leak into legacy unconditional records");
    require(simpilot::CustomHotkeySettings::read(document) == settings, "Names, modes, priority and order survive reload");
    const auto reject = [&](std::wstring_view key, std::wstring_view value) {
        auto changed = document;
        try { changed.set(L"CustomGlobalHotkeys", key, value);
            (void)simpilot::CustomHotkeySettings::read(changed); return false; } catch (...) { return true; }
    };
    require(reject(L"ConditionalGlobalHotKey1Priority", L"1000"), "Priority bounds validated during import");
    require(reject(L"ConditionalGlobalHotKey1Priority", L"25"), "Tied overlapping scenarios rejected");
    require(reject(L"ConditionalGlobalHotKey1UsePriority", L"0"), "Removing explicit precedence reintroduces conflict");
    require(reject(L"ConditionalGlobalHotKey1RequireRunning", L"1"), "Mixed process modes rejected");
    require(reject(L"ConditionalGlobalHotKey1Name", std::wstring(129, L'x')), "Overlong names rejected");
    require(reject(L"ConditionalGlobalHotKey1Name", L"line\nfield=1"), "Names cannot inject INI lines");
    require(reject(L"ConditionalGlobalHotKeyVersion", L"1"), "Scenario records cannot be downgraded to old semantics");
    require(reject(L"ConditionalGlobalHotKeyVersion", L"3"), "Unknown formats rejected");
    for (const auto suffix : {L"RequireForeground", L"Priority", L"UsePriority"}) {
        auto changed = document;
        changed.erase(L"ConditionalGlobalHotKey1" + std::wstring(suffix));
        bool rejected = false;
        try { (void)simpilot::CustomHotkeySettings::read(changed); } catch (...) { rejected = true; }
        require(rejected, "Incomplete v2 scenario fields never become unconditional");
    }
    const auto root = std::filesystem::temp_directory_path() /
        (L"Simpilot-scenario-backup-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root / L"Config");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{root};
    const auto bytes = document.serialize();
    simpilot::SettingsSnapshot snapshot;
    snapshot.files["Config/Setting.ini"] = {bytes.begin(), bytes.end()};
    std::vector<std::wstring> warnings;
    simpilot::inspect_custom_hotkey_backup(snapshot, true, warnings);
    require(!warnings.empty(), "Missing scenario program paths are preserved with warnings");
    simpilot::SettingsBackup::write(snapshot, root / L"scenarios.simpilot-backup");
    const auto validator = [](const auto& value) { std::vector<std::wstring> ignored; simpilot::inspect_custom_hotkey_backup(value, false, ignored); };
    const auto restored = simpilot::SettingsBackup::read(root / L"scenarios.simpilot-backup");
    auto original = document;
    original.set(L"General", L"Language", L"en-US");
    require(original.save(root / L"Config/Setting.ini"), "Prepare scenario restoration target");
    simpilot::SettingsBackup::stage(root, restored, validator);
    require(simpilot::SettingsBackup::recover(root, validator), "Restore scenario transaction");
    const auto reloaded = simpilot::SettingsDocument::load(root / L"Config/Setting.ini");
    require(simpilot::CustomHotkeySettings::read(reloaded) == settings, "Global restore preserves complete scenario rules and labels");
    settings.items = {fallback};
    settings.items[0].binding.activation = {};
    settings.items[0].name = L"普通规则";
    settings.write(document);
    require(document.get(L"ConditionalGlobalHotKeyVersion") == L"1"
        && !document.get(L"ConditionalGlobalHotKey1Priority")
        && !document.get(L"ConditionalGlobalHotKey1RequireForeground")
        && document.get(L"CustomGlobalHotKey1Name") == L"普通规则", "Removing scenarios restores compatible format and clears owned fields");
    require(document.get(L"Unowned") == L"yes" && document.get(L"ConditionalGlobalHotKey1FutureField") == L"keep",
        "Scenario saves preserve unrelated future fields");
}

void module_tests() {
    const auto document = simpilot::SettingsDocument::parse(
        "[CustomGlobalHotkeys]\nCustomGlobalHotKeyCount=1\nCustomGlobalHotKey1Code=3,88\n"
        "CustomGlobalHotKey1Program=C:\\Windows\\notepad.exe\nCustomGlobalHotKey1Enabled=1\nCustomGlobalHotKey1Name=Workspace search\n");
    simpilot::SettingsParticipantRegistry participants;
    simpilot::HotkeyRegistry hotkeys;
    simpilot::KeyboardManager keyboard;
    auto module = simpilot::make_custom_hotkey_module(document, L".", participants,
        hotkeys, keyboard, {}, {});
    module->start();
    module->start();
    require(hotkeys.sections.size() == 1 && participants.size() == 1, "Idempotent start");
    simpilot::SettingsSession session(participants, document);
    auto drafts = hotkeys.editable_drafts();
    require(drafts.size() == 1 && !drafts[0].common_row, "Custom section owns its variable-length rows");
    auto next = drafts[0].read();
    next.binding.gesture = simpilot::HotKeyGesture{MOD_CONTROL, VK_F24};
    drafts[0].write(next);
    require(session.dirty(), "Draft edit changes participant dirty state");
    const auto runtime_key = [&] {
        UINT key = 0;
        hotkeys.refresh([&](int, const auto& binding) { key = binding.gesture->virtual_key; return true; });
        return key;
    };
    require(runtime_key() == L'X', "Draft edit does not mutate runtime");
    require(!session.apply([](const auto&) { return false; }), "Injected INI failure");
    require(runtime_key() == L'X' && session.dirty(), "Rollback restores runtime and retains draft");
    require(session.apply([](const auto& candidate) {
        return candidate.get(L"CustomGlobalHotKey1Code") == L"2,135";
    }), "Successful transaction writes legacy key");
    require(runtime_key() == VK_F24 && !session.dirty(), "Apply publishes new runtime");
    auto conditional = drafts[0].read();
    conditional.binding.activation.locks[0] = simpilot::ToggleRequirement::off;
    conditional.binding.activation.require_running_program = true;
    conditional.binding.activation.programs = {L"C:\\Apps\\scene.exe"};
    conditional.binding.activation.priority = 25;
    drafts[0].write(conditional);
    const auto runtime_conditions = [&] {
        simpilot::HotkeyActivationConditions result;
        hotkeys.refresh([&](int, const auto& binding) { result = binding.activation; return true; });
        return result;
    };
    require(!session.apply([](const auto&) { return false; }) && !runtime_conditions().conditional(),
        "Save failure rolls back condition state without changing the live hotkey");
    require(session.dirty() && drafts[0].read() == conditional, "Failed save retains the conditional draft");
    require(session.apply([](const auto& value) {
        return value.get(L"CustomGlobalHotKeyCount") == L"0"
            && value.get(L"ConditionalGlobalHotKeyCount") == L"1"
            && value.get(L"ConditionalGlobalHotKey1Name") == L"Workspace search"
            && value.get(L"ConditionalGlobalHotKeyVersion") == L"2";
    }) && runtime_conditions() == conditional.binding.activation, "Successful apply publishes condition state");

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    const auto instance = GetModuleHandleW(nullptr);
    const auto parent = CreateWindowW(L"STATIC", L"test", WS_OVERLAPPEDWINDOW,
        0, 0, 900, 700, nullptr, nullptr, instance, nullptr);
    require(parent != nullptr, "Create isolated settings host");
    const simpilot::Localization english("en-US");
    auto page = simpilot::make_hotkey_settings_page(hotkeys, keyboard, {}, {});
    int changes = 0;
    page->create({instance, parent, 96, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        english, [&] { ++changes; }});
    page->layout({0, 0, 700, 500}, 144, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    page->show(true);
    const auto section = FindWindowExW(
        FindWindowExW(parent, nullptr, L"Simpilot.HotkeySettingsPage", nullptr),
        nullptr, L"Simpilot.CustomHotkeySection", nullptr);
    require(section != nullptr, "Common page hosts module section");
    for (const UINT dpi : {96U, 120U, 144U, 192U}) {
        page->layout({0, 0, MulDiv(760, dpi, 96), MulDiv(600, dpi, 96)}, dpi,
            static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        const auto list = GetDlgItem(section, 4);
        for (int column = 0; column < 6; ++column) {
            HDITEMW header{.mask = HDI_FORMAT};
            require(Header_GetItem(ListView_GetHeader(list), column, &header)
                && (header.fmt & HDF_STRING), "Every custom hotkey header draws its title");
        }
        require(ListView_GetColumnWidth(list, 0) == MulDiv(64, dpi, 96),
            "Custom enable column matches the shared sliding switch width");
        wchar_t displayed_name[128]{};
        ListView_GetItemText(list, 0, 2, displayed_name, 128);
        require(std::wstring_view(displayed_name) == L"Workspace search", "Rule name is visible in the hotkey list");
        RECT client{}, button{}, heading{};
        GetClientRect(section, &client);
        GetWindowRect(GetDlgItem(section, 1), &button);
        GetWindowRect(FindWindowExW(section, nullptr, L"STATIC", nullptr), &heading);
        require(button.bottom - button.top == MulDiv(32, dpi, 96)
            && heading.right < button.left && heading.top < button.bottom,
            "Section heading and compact toolbar share one row without overlap");
        LOGFONTW label_font{};
        GetObjectW(reinterpret_cast<HFONT>(SendMessageW(
            FindWindowExW(section, nullptr, L"STATIC", nullptr), WM_GETFONT, 0, 0)),
            sizeof(label_font), &label_font);
        require(label_font.lfHeight == -MulDiv(14, dpi, 96) && label_font.lfWeight == FW_SEMIBOLD,
            "Custom section uses a section heading, not a duplicate page title");
    }
    for (const auto locale : {"en-US", "zh-CN", "zh-TW"}) {
        const simpilot::Localization localization(locale);
        require(localization.text("custom_hotkey.invalid_target") != L"[missing translation]",
            "Module language resources are linked");
        require(localization.text("hotkey.activation.heading") != L"[missing translation]"
            && localization.text("custom_hotkey.conditions_hint") != L"[missing translation]",
            "Activation labels exist in all three languages");
        page->refresh_language(localization);
    }
    page->refresh_language(english);
    page.reset();
    DestroyWindow(parent);
    session.cancel();
    module->stop();
    module->stop();
    require(!drafts[0].active() && hotkeys.sections.size() == 0 && participants.size() == 0,
        "Stop revokes draft and section callbacks");
    require(runtime_key() == 0, "Stop revokes all runtime bindings");
}
UINT dialog_dpi = 96;
bool dialog_empty_programs = false;
bool dialog_pick_program = false;
std::wstring dialog_program_path;
unsigned picker_attempts = 0;
std::string dialog_failure;
void CALLBACK fill_program_picker(HWND, UINT, UINT_PTR, DWORD) {
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
        wchar_t type[128]{};
        GetClassNameW(window, type, static_cast<int>(std::size(type)));
        if (std::wstring_view(type) != L"#32770" || !IsWindowVisible(window)) return TRUE;
        if (++picker_attempts > 5) {
            SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
        } else {
            // The Windows 11 picker uses a nested filename edit, not the
            // legacy CDM_SETCONTROLTEXT protocol on the top-level dialog.
            EnumChildWindows(window, [](HWND child, LPARAM) -> BOOL {
                wchar_t type[128]{};
                GetClassNameW(child, type, static_cast<int>(std::size(type)));
                if (GetDlgCtrlID(child) == cmb13 && std::wstring_view(type) == L"Edit") {
                    SetWindowTextW(child, dialog_program_path.c_str());
                    return FALSE;
                }
                return TRUE;
            }, 0);
            SendMessageW(GetDlgItem(window, IDOK), BM_CLICK, 0, 0);
        }
        return TRUE;
    }, 0);
}
HWND descendant(HWND parent, int identifier) {
    struct Search { int identifier; HWND result; } search{identifier, nullptr};
    EnumChildWindows(parent, [](HWND window, LPARAM data) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(data);
        if (GetDlgCtrlID(window) == value.identifier) { value.result = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
void CALLBACK exercise_dialog(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND window = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM data) -> BOOL {
        wchar_t type[128]{};
        GetClassNameW(candidate, type, static_cast<int>(std::size(type)));
        if (std::wstring_view(type) == L"Simpilot.CustomHotKeyDialog") {
            *reinterpret_cast<HWND*>(data) = candidate;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&window));
    if (!window) return;
    KillTimer(nullptr, timer);
    try {
        RECT bounds{};
        GetWindowRect(window, &bounds);
        SendMessageW(window, WM_DPICHANGED, MAKELONG(dialog_dpi, dialog_dpi), reinterpret_cast<LPARAM>(&bounds));
        const auto save = GetDlgItem(window, 1);
        const auto condition = descendant(window, 310);
        const auto list = descendant(window, 313);
        require(save && condition && list, "Condition controls exist");
        require(SendMessageW(list, LB_GETCOUNT, 0, 0) == (dialog_empty_programs ? 0 : 2), "Associated paths are displayed");
        if (dialog_empty_programs) {
            SendMessageW(condition, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(310, BN_CLICKED), reinterpret_cast<LPARAM>(condition));
            require(!IsWindowEnabled(save), "UI rejects an enabled empty program condition");
            if (dialog_pick_program) {
                picker_attempts = 0;
                const auto picker_timer = SetTimer(nullptr, 0, 200, fill_program_picker);
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(311, BN_CLICKED), 0);
                KillTimer(nullptr, picker_timer);
                require(SendMessageW(list, LB_GETCOUNT, 0, 0) == 1 && IsWindowEnabled(save),
                    "Selecting an EXE in the native picker adds a valid program condition");
                SendMessageW(list, LB_SETCURSEL, 0, 0);
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(312, BN_CLICKED), 0);
                require(SendMessageW(list, LB_GETCOUNT, 0, 0) == 0 && !IsWindowEnabled(save),
                    "Removing the final program disables saving the enabled condition");
            }
            SendMessageW(condition, BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(310, BN_CLICKED), reinterpret_cast<LPARAM>(condition));
        }
        RECT previous{};
        for (int i = 0; i < 3; ++i) {
            const auto combo = descendant(window, 320 + i);
            require(combo && SendMessageW(combo, CB_GETCOUNT, 0, 0) == 3, "Lock selector has exactly three choices");
            SendMessageW(combo, CB_SETCURSEL, i == 1 ? 1 : 2, 0);
            RECT rectangle{};
            GetWindowRect(combo, &rectangle);
            require(rectangle.right - rectangle.left >= MulDiv(140, dialog_dpi, 96), "Lock selector scales at requested DPI");
            require(i == 0 || previous.right < rectangle.left, "Condition columns never overlap");
            previous = rectangle;
            LOGFONTW font{};
            GetObjectW(reinterpret_cast<HFONT>(SendMessageW(combo, WM_GETFONT, 0, 0)), sizeof(font), &font);
            require(font.lfHeight == -MulDiv(14, dialog_dpi, 96), "Condition font follows DPI changes");
        }
        require(IsWindowEnabled(save), "Valid conditions allow save");
        const auto foreground = descendant(window, 314);
        const auto priority = descendant(window, 315);
        const auto priority_edit = descendant(window, 316);
        const auto name = descendant(window, 317);
        require(foreground && priority && priority_edit && name, "Scenario and name controls exist");
        SetWindowTextW(name, L"工作—搜索");
        if (!dialog_empty_programs) {
            SendMessageW(foreground, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(314, BN_CLICKED), reinterpret_cast<LPARAM>(foreground));
            require(SendMessageW(condition, BM_GETCHECK, 0, 0) == BST_UNCHECKED,
                "Foreground selection switches off the background mode");
        }
        SendMessageW(priority, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(315, BN_CLICKED), reinterpret_cast<LPARAM>(priority));
        SetWindowTextW(priority_edit, L"");
        require(!IsWindowEnabled(save), "Enabled empty priority cannot be saved");
        SetWindowTextW(priority_edit, L"1000");
        require(!IsWindowEnabled(save), "Out-of-range priority cannot be saved");
        SetWindowTextW(priority_edit, L"125");
        require(IsWindowEnabled(save), "Named foreground scenario and explicit precedence can be saved");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(1, BN_CLICKED), reinterpret_cast<LPARAM>(save));
    } catch (const std::exception& error) {
        dialog_failure = error.what();
        SendMessageW(window, WM_CLOSE, 0, 0);
    }
}
void dialog_tests() {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    dialog_program_path = executable;
    simpilot::CustomGlobalHotKey initial;
    initial.binding.gesture = simpilot::HotKeyGesture{MOD_CONTROL, VK_F24};
    initial.program_path = executable;
    simpilot::KeyboardManager keyboard;
    for (const auto locale : {"zh-CN", "zh-TW", "en-US"}) for (const UINT dpi : {96U, 120U, 144U, 192U}) {
        dialog_dpi = dpi;
        dialog_empty_programs = dpi == 96;
        dialog_pick_program = dpi == 96 && std::string_view(locale) == "en-US";
        initial.binding.activation.require_running_program = !dialog_empty_programs;
        initial.binding.activation.programs = dialog_empty_programs ? std::vector<std::wstring>{}
            : std::vector<std::wstring>{L"C:\\Apps\\中文目录\\editor.exe", L"C:\\Apps\\A-long-application-name\\background.exe"};
        const auto timer = SetTimer(nullptr, 0, 30, exercise_dialog);
        const auto result = simpilot::CustomHotKeyDialog::show_modal(GetModuleHandleW(nullptr), nullptr,
            locale, keyboard, std::filesystem::path(executable).parent_path(), &initial);
        KillTimer(nullptr, timer);
        if (!dialog_failure.empty()) throw std::runtime_error(dialog_failure);
        require(result && result->binding.activation.allowed_lock_states() == (1U << 2)
            && result->binding.activation.programs == initial.binding.activation.programs
            && !result->binding.activation.require_running_program
            && result->binding.activation.require_foreground_program == !dialog_empty_programs
            && result->binding.activation.priority == 125U && result->name == L"工作—搜索",
            "Native dialog preserves conditions through save in all languages and DPIs");
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--preview") {
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
            InitCommonControlsEx(&controls);
            wchar_t path[32768]{};
            GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
            simpilot::CustomGlobalHotKey item;
            item.binding.gesture = simpilot::HotKeyGesture{MOD_CONTROL | MOD_SHIFT, VK_F24};
            item.program_path = path;
            item.binding.activation.locks[1] = simpilot::ToggleRequirement::on;
            simpilot::KeyboardManager keyboard;
            const auto edited = simpilot::CustomHotKeyDialog::show_modal(GetModuleHandleW(nullptr), nullptr,
                argc > 2 ? argv[2] : "zh-CN", keyboard, std::filesystem::path(path).parent_path(), &item);
            if (edited) {
                std::cout << "Saved preview only: lock mask=" << unsigned(edited->binding.activation.allowed_lock_states())
                    << ", process condition=" << edited->binding.activation.require_running_program
                    << ", paths=" << edited->binding.activation.programs.size() << '\n';
            }
            return 0;
        }
        codec_tests();
        conditional_codec_tests();
        scenario_codec_tests();
        switch_image_tests();
        module_tests();
        dialog_tests();
        std::cout << "Custom hotkey module tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
