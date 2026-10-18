#include "input_method_module_internal.hpp"
#include "input_method_page.hpp"
#include "settings_page_test_support.hpp"
#include "test_backend.hpp"

#include <commctrl.h>

#include <iostream>
#include <utility>

namespace {
using namespace simpilot;
using namespace simpilot::input_method;
using settings_page_test::require;

std::function<void(HWND)> dialog_action;
std::string dialog_failure;

void CALLBACK dialog_timer(HWND, UINT, UINT_PTR, DWORD) {
    auto window = FindWindowW(L"Simpilot.InputMethodRuleDialog", nullptr);
    if (!window) return;
    try {
        auto action = std::exchange(dialog_action, {});
        if (action) action(window);
    } catch (const std::exception& error) {
        dialog_failure = error.what();
        SendMessageW(window, WM_CLOSE, 0, 0);
    }
}

void pump(DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

void page_tests() {
    Settings draft;
    for (int i = 0; i < 40; ++i)
        draft.rules.push_back({L"C:\\Apps\\An application with a long name\\Editor"
            + std::to_wstring(i) + L".exe", Policy::fixed, L"tsf:pinyin", Mode::chinese});
    test::Backend backend;
    SettingsRegistry registry;
    auto contribution = registry.add("input_method", 45, {
        "settings.tab.input_method", [&] {
            return make_settings_page(draft, {
                [&]() -> const std::vector<Profile>& { return backend.catalog; },
                [&] { ++backend.enumerations; },
                [] { return Status{"settings.input_method.state_failed", L"C:\\App.exe", ERROR_ACCESS_DENIED}; }});
        }});
    settings_page_test::check_page(registry, L"Simpilot.InputMethodSettingsPage");
    settings_page_test::Host host;
    Localization localization(UiLanguage::simplified_chinese);
    auto page = make_settings_page(draft, {
        [&]() -> const std::vector<Profile>& { return backend.catalog; },
        [&] { ++backend.enumerations; }, [] { return Status{}; }});
    int changes = 0;
    page->create({GetModuleHandleW(nullptr), host.window, 96,
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), localization, [&] { ++changes; }});
    page->layout({0, 0, 650, 440}, 96, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    page->show(true);
    const auto window = FindWindowExW(host.window, nullptr, L"Simpilot.InputMethodSettingsPage", nullptr);
    const auto list = GetDlgItem(window, 6);
    require(ListView_GetItemCount(list) == 40, "rule list missing");
    int column_width = 0;
    for (int index = 0; index < 5; ++index) {
        LVCOLUMNW column{.mask = LVCF_SUBITEM};
        require(ListView_GetColumn(list, index, &column) && column.iSubItem == index,
            "list column is not bound to its own subitem");
        wchar_t content[1024]{};
        ListView_GetItemText(list, 0, index, content, 1024);
        require(content[0] != L'\0', "populated rule cell is blank");
        column_width += ListView_GetColumnWidth(list, index);
    }
    RECT list_bounds{};
    GetClientRect(list, &list_bounds);
    require(column_width <= list_bounds.right, "default width hides rule status column");
    page->layout({0, 0, 975, 660}, 144,
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    for (int index = 0; index < 5; ++index) {
        wchar_t content[1024]{};
        ListView_GetItemText(list, 0, index, content, 1024);
        require(content[0] != L'\0', "relayout erased populated list cell");
    }
    require(GetWindowLongPtrW(list, GWL_STYLE) & WS_VSCROLL, "dense rule list not scrollable");
    ListView_SetItemState(list, 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    require(IsWindowEnabled(GetDlgItem(window, 3)), "selected rule not editable");
    SendMessageW(GetDlgItem(window, 1), BM_CLICK, 0, 0);
    require(draft.enabled && changes == 1, "toggle did not update draft or dirty callback");
    SendMessageW(GetDlgItem(window, 4), BM_CLICK, 0, 0);
    require(draft.rules.size() == 39 && changes == 2, "delete did not update draft");
    const auto before = changes;
    SendMessageW(GetDlgItem(window, 5), BM_CLICK, 0, 0);
    require(backend.enumerations == 1 && changes == before, "refresh marked draft dirty");

    for (const auto policy : {Policy::remember, Policy::fixed, Policy::ignore}) {
        Rule initial{L"C:\\Apps\\Editor.exe", policy, L"tsf:pinyin", Mode::chinese};
        dialog_action = [policy](HWND dialog) {
            const bool fixed = policy == Policy::fixed;
            require((IsWindowVisible(GetDlgItem(dialog, 103)) != FALSE) == fixed,
                "profile visibility does not follow policy");
            require((IsWindowVisible(GetDlgItem(dialog, 104)) != FALSE) == fixed,
                "mode visibility does not follow policy");
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        };
        const auto timer = SetTimer(nullptr, 0, 30, dialog_timer);
        require(timer != 0, "could not start dialog test timer");
        auto result = show_rule_dialog(GetModuleHandleW(nullptr), host.window, localization,
            initial, backend.catalog, {});
        KillTimer(nullptr, timer);
        require(dialog_failure.empty(), dialog_failure.c_str());
        require(!result, "cancel committed rule");
    }
    dialog_action = [](HWND dialog) {
        SendMessageW(GetDlgItem(dialog, 102), CB_SETCURSEL, static_cast<int>(Policy::fixed), 0);
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(102, CBN_SELCHANGE), 0);
        require(IsWindowVisible(GetDlgItem(dialog, 103)), "fixed controls not shown");
        SendMessageW(GetDlgItem(dialog, 103), CB_SETCURSEL, 0, 0);
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(103, CBN_SELCHANGE), 0);
        require(SendMessageW(GetDlgItem(dialog, 104), CB_GETCOUNT, 0, 0) == 1,
            "English keyboard offers Chinese mode");
        SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    };
    const auto timer = SetTimer(nullptr, 0, 30, dialog_timer);
    auto saved = show_rule_dialog(GetModuleHandleW(nullptr), host.window, localization,
        {L"C:\\Apps\\Editor.exe"}, backend.catalog, {});
    KillTimer(nullptr, timer);
    require(saved && saved->policy == Policy::fixed && saved->mode == Mode::english,
        "fixed rule was not saved");
    require(dialog_failure.empty(), dialog_failure.c_str());

    auto check_dialog = [&](Rule initial, const std::vector<Profile>& profiles,
        const std::vector<Rule>& others, std::function<void(HWND)> action) {
        dialog_action = std::move(action);
        const auto callback = SetTimer(nullptr, 0, 30, dialog_timer);
        require(callback != 0, "could not start edge-case dialog timer");
        auto result = show_rule_dialog(
            GetModuleHandleW(nullptr), host.window, localization, initial, profiles, others);
        KillTimer(nullptr, callback);
        require(dialog_failure.empty(), dialog_failure.c_str());
        return result;
    };
    const Rule unavailable{L"C:\\Apps\\Removed.exe", Policy::fixed, L"removed-profile", Mode::chinese};
    auto preserved = check_dialog(unavailable, backend.catalog, {}, [](HWND dialog) {
        const auto combo = GetDlgItem(dialog, 103);
        require(SendMessageW(combo, CB_GETCOUNT, 0, 0) == 3,
            "unavailable saved profile not retained in editor");
        SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    require(preserved && *preserved == unavailable, "no-op edit replaced unavailable profile");

    auto no_profiles = check_dialog(
        {L"C:\\Apps\\Editor.exe", Policy::fixed}, {}, {}, [](HWND dialog) {
            require(SendMessageW(GetDlgItem(dialog, 103), CB_GETCOUNT, 0, 0) == 0,
                "empty system catalog invented a profile");
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            require(IsWindow(dialog) && GetWindowTextLengthW(GetDlgItem(dialog, 115)) > 0,
                "missing fixed profile accepted without inline error");
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        });
    require(!no_profiles, "invalid empty-catalog rule committed");

    auto duplicate = check_dialog({L"c:\\apps\\EDITOR.exe"}, backend.catalog,
        {{L"C:\\Apps\\Editor.exe"}}, [](HWND dialog) {
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            require(IsWindow(dialog) && GetWindowTextLengthW(GetDlgItem(dialog, 115)) > 0,
                "case-insensitive duplicate accepted by editor");
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        });
    require(!duplicate, "duplicate rule committed");

    auto ignored = check_dialog(unavailable, backend.catalog, {}, [](HWND dialog) {
        SendMessageW(GetDlgItem(dialog, 102), CB_SETCURSEL, static_cast<int>(Policy::ignore), 0);
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(102, CBN_SELCHANGE), 0);
        require(!IsWindowVisible(GetDlgItem(dialog, 103))
            && !IsWindowEnabled(GetDlgItem(dialog, 104)), "ignore retained fixed controls");
        SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    require(ignored && ignored->policy == Policy::ignore && ignored->profile_id.empty(),
        "policy change kept a stale fixed-profile identifier");

    page.reset();
    draft.rules.clear();
    backend.catalog.clear();
    page = make_settings_page(draft, {
        [&]() -> const std::vector<Profile>& { return backend.catalog; },
        [] {}, [] { return Status{}; }});
    page->create({GetModuleHandleW(nullptr), host.window, 96,
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), localization, {}});
    page->show(true);
    const auto empty = FindWindowExW(host.window, nullptr, L"Simpilot.InputMethodSettingsPage", nullptr);
    require(ListView_GetItemCount(GetDlgItem(empty, 6)) == 0
        && !IsWindowEnabled(GetDlgItem(empty, 3)) && !IsWindowEnabled(GetDlgItem(empty, 4)),
        "empty page retained rule commands");
}

void module_tests() {
    const auto directory = std::filesystem::temp_directory_path()
        / (L"Simpilot-InputMethodModule-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    UiDispatcher dispatcher(GetModuleHandleW(nullptr));
    SettingsRegistry pages;
    SettingsParticipantRegistry participants;
    auto mock = std::make_unique<test::Backend>();
    auto* backend = mock.get();
    backend->activate(L"C:\\Apps\\Editor.exe");
    backend->state[normalize_path(L"C:\\Apps\\Editor.exe")] = {L"layout:00000409", Mode::english};
    auto module = make_module(directory, dispatcher, pages, participants, {}, std::move(mock));
    module->start();
    require(pages.size() == 1 && participants.size() == 1, "module did not contribute");
    pump(300);
    require(backend->polls == 0 && !std::filesystem::exists(directory / L"InputMethodHistory.ini"),
        "disabled module monitored or wrote history");
    settings_page_test::Host host;
    Localization localization(UiLanguage::english);
    std::unique_ptr<ISettingsPage> page;
    pages.visit([&](const auto&, const auto& value) { page = value.create(); });
    SettingsSession session(participants, {});
    page->create({GetModuleHandleW(nullptr), host.window, 96,
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), localization, {}});
    page->layout({0, 0, 650, 440}, 96, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    page->show(true);
    const auto window = FindWindowExW(host.window, nullptr, L"Simpilot.InputMethodSettingsPage", nullptr);
    SendMessageW(GetDlgItem(window, 1), BM_CLICK, 0, 0);
    require(session.dirty(), "module draft is not dirty");
    pump(300);
    require(backend->polls == 0, "draft enabled runtime before apply");
    require(!session.apply([](const SettingsDocument&) { return false; }), "failed INI save accepted");
    pump(300);
    require(backend->polls == 0, "rollback left runtime enabled");
    SettingsDocument stored;
    require(session.apply([&](const SettingsDocument& candidate) {
        stored = candidate;
        return true;
    }), "module commit failed");
    require(Settings::read(stored).enabled && !session.dirty(), "commit baseline incorrect");
    pump(500);
    require(backend->polls > 0, "applied enable did not monitor");
    const auto before_dirty = session.dirty();
    backend->activate(L"C:\\Apps\\Browser.exe");
    backend->state[normalize_path(L"C:\\Apps\\Browser.exe")] = {L"tsf:pinyin", Mode::chinese};
    pump(250);
    require(std::filesystem::exists(directory / L"InputMethodHistory.ini")
        && session.dirty() == before_dirty, "automatic history affected settings draft");
    require(History::read(SettingsDocument::load(directory / L"InputMethodHistory.ini"))
        .states.contains(normalize_path(L"C:\\Apps\\Editor.exe")),
        "module did not persist outgoing application");
    SendMessageW(GetDlgItem(window, 1), BM_CLICK, 0, 0);
    require(!session.apply([](const SettingsDocument&) { return false; }),
        "failed disable save accepted");
    const auto polls_before_rollback = backend->polls;
    pump(450);
    require(backend->polls > polls_before_rollback, "failed disabling did not resume polling");
    require(session.apply([&](const SettingsDocument& candidate) {
        stored = candidate;
        return true;
    }), "disable transaction failed");
    const auto polls_after_disable = backend->polls;
    pump(400);
    require(backend->polls == polls_after_disable && !Settings::read(stored).enabled,
        "committed disabling retained monitor");
    SendMessageW(GetDlgItem(window, 1), BM_CLICK, 0, 0);
    session.cancel();
    require(backend->polls == polls_after_disable, "cancel enabled uncommitted draft");
    page.reset();
    module->stop();
    const auto polls = backend->polls;
    pump(400);
    require(backend->polls == polls && pages.size() == 0 && participants.size() == 0,
        "stop retained contributions or timer callback");
    module->stop();
    std::filesystem::remove_all(directory);

    const auto failure_dir = std::filesystem::temp_directory_path()
        / (L"Simpilot-InputMethodFailure-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(failure_dir);
    SettingsDocument config;
    Settings{true, {}}.write(config);
    require(config.save(failure_dir / L"Setting.ini"), "failed to save test settings");
    dispatcher.close();
    auto failing = make_module(failure_dir, dispatcher, pages, participants, {},
        std::make_unique<test::Backend>());
    bool threw = false;
    try { failing->start(); } catch (...) { threw = true; }
    require(threw && pages.size() == 0 && participants.size() == 0,
        "startup failure did not clean contributions");
    std::filesystem::remove_all(failure_dir);

    const auto malformed_dir = std::filesystem::temp_directory_path()
        / (L"Simpilot-InputMethodMalformed-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(malformed_dir);
    UiDispatcher valid_dispatcher(GetModuleHandleW(nullptr));
    SettingsDocument malformed = SettingsDocument::parse(
        "[InputMethod]\nInputMethodRuleCount=-1\nFutureInputMethodKey=keep\n");
    require(malformed.save(malformed_dir / L"Setting.ini"), "invalid fixture not saved");
    auto malformed_module = make_module(malformed_dir, valid_dispatcher,
        pages, participants, {}, std::make_unique<test::Backend>());
    malformed_module->start();
    SettingsSession malformed_session(participants, malformed);
    require(malformed_session.apply([](const SettingsDocument& candidate) {
        return candidate.serialize().find("InputMethodRuleCount=-1") != std::string::npos
            && candidate.serialize().find("FutureInputMethodKey=keep") != std::string::npos;
    }), "unrelated saving overwrote or rejected preserved invalid module data");
    malformed_session.cancel();
    malformed_module->stop();
    std::filesystem::remove_all(malformed_dir);

    const auto restart_dir = std::filesystem::temp_directory_path()
        / (L"Simpilot-InputMethodRestart-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(restart_dir);
    SettingsDocument enabled;
    Settings{true, {}}.write(enabled);
    require(enabled.save(restart_dir / L"Setting.ini"), "restart configuration not saved");
    auto history_file = SettingsDocument::parse(
        "; keep history comment\n[InputMethodHistory]\nFutureHistoryField=keep\n");
    const auto editor_path = normalize_path(L"C:\\Apps\\Editor.exe");
    History{{{editor_path, {L"tsf:pinyin", Mode::chinese}}}}.write(history_file);
    require(history_file.save(restart_dir / L"InputMethodHistory.ini"), "restart history not saved");
    auto first_backend = std::make_unique<test::Backend>();
    auto* first = first_backend.get();
    first->activate(L"C:\\Apps\\Editor.exe");
    first->state[editor_path] = {L"layout:00000409", Mode::english};
    auto first_run = make_module(restart_dir, valid_dispatcher, pages, participants,
        {}, std::move(first_backend));
    first_run->start();
    pump(750);
    require(first->state.at(editor_path) == State{L"tsf:pinyin", Mode::chinese},
        "module startup did not restore disk history");
    first->state[editor_path].mode = Mode::english;
    pump(250);
    first_run->stop();
    const auto saved_history = SettingsDocument::load(restart_dir / L"InputMethodHistory.ini");
    require(History::read(saved_history).states.at(editor_path).mode == Mode::english
        && saved_history.serialize().find("FutureHistoryField=keep") != std::string::npos
        && saved_history.serialize().find("; keep history comment") != std::string::npos,
        "shutdown lost manual state or unknown disk contents");
    auto second_backend = std::make_unique<test::Backend>();
    auto* second = second_backend.get();
    second->activate(L"C:\\Apps\\Editor.exe");
    second->state[editor_path] = {L"tsf:pinyin", Mode::chinese};
    auto second_run = make_module(restart_dir, valid_dispatcher, pages, participants,
        {}, std::move(second_backend));
    second_run->start();
    pump(500);
    require(second->state.at(editor_path).mode == Mode::english,
        "restarted module did not restore last manual mode");
    second_run->stop();
    std::filesystem::remove_all(restart_dir);
}

} // namespace

int main() {
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        page_tests();
        module_tests();
        std::cout << "input method UI, transaction and lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
