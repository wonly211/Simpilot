#include "input_method_page.hpp"
#include "running_applications.hpp"
#include "settings_page_test_support.hpp"

#include <commctrl.h>

#include <atomic>
#include <iostream>
#include <thread>
#include <utility>

namespace {
using namespace simpilot;
using namespace simpilot::input_method;
using settings_page_test::require;

constexpr auto picker_class = L"Simpilot.InputMethodApplicationDialog";
constexpr auto rule_class = L"Simpilot.InputMethodRuleDialog";
constexpr auto page_class = L"Simpilot.InputMethodSettingsPage";
std::function<void()> tick;
std::string failure;
ULONGLONG deadline = 0;

void CALLBACK timer_action(HWND, UINT, UINT_PTR, DWORD) {
    try {
        if (GetTickCount64() >= deadline) throw std::runtime_error("dialog test timed out");
        if (tick) tick();
    } catch (const std::exception& error) {
        failure = error.what();
        tick = {};
        EndMenu();
        if (auto dialog = FindWindowW(picker_class, nullptr)) PostMessageW(dialog, WM_CLOSE, 0, 0);
        if (auto dialog = FindWindowW(rule_class, nullptr)) PostMessageW(dialog, WM_CLOSE, 0, 0);
    }
}

class Timer final {
public:
    explicit Timer(std::function<void()> callback) {
        failure.clear();
        deadline = GetTickCount64() + 5000;
        tick = std::move(callback);
        id_ = SetTimer(nullptr, 0, 20, timer_action);
        require(id_ != 0, "cannot start test timer");
    }
    ~Timer() { KillTimer(nullptr, id_); tick = {}; }
    void check() const { require(failure.empty(), failure.c_str()); }
private:
    UINT_PTR id_;
};

ApplicationSnapshot fixture() {
    return {{
        {L"C:\\Apps\\Zulu.exe", {L"Zulu window"}, true},
        {L"C:\\Apps\\Editor.exe", {L"First document"}, true},
        {L"c:\\apps\\EDITOR.exe", {L"Second document", L"First document"}, true},
        {L"C:\\Other\\Editor.exe", {L"Other installation"}, true},
        {L"C:\\Apps\\Worker.exe", {}, false},
        {L"relative.exe", {L"Invalid"}, true},
        {L"", {L"Inaccessible"}, true}
    }};
}

void select_row(HWND dialog, int row) {
    const auto list = GetDlgItem(dialog, 204);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED,
        LVIS_SELECTED | LVIS_FOCUSED);
}

bool ready(HWND dialog) {
    return dialog && IsWindowEnabled(GetDlgItem(dialog, 202));
}

void model_tests() {
    const auto merged = merge_running_applications(fixture());
    require(merged.applications.size() == 4, "invalid paths not skipped or duplicates not merged");
    require(merged.applications[0].executable_path == L"C:\\Apps\\Editor.exe"
        && merged.applications[1].executable_path == L"C:\\Other\\Editor.exe",
        "same-name applications not sorted by full path");
    require(merged.applications[0].window_titles.size() == 2, "merged window titles lost or duplicated");
    require(filter_running_applications(merged, false, L"").size() == 3,
        "default filter includes background processes");
    require(filter_running_applications(merged, true, L"").size() == 4,
        "background filter omitted accessible process");
    require(filter_running_applications(merged, false, L"SECOND DOCUMENT") == std::vector<std::size_t>{0},
        "search did not include every window title");
    require(filter_running_applications(merged, true, L"WORKER") == std::vector<std::size_t>{2},
        "case-insensitive executable search failed");
    require(filter_running_applications(merged, false, L"c:\\OTHER") == std::vector<std::size_t>{1},
        "full path search failed");
    require(filter_running_applications(merged, false, L"no match").empty(), "search ignored");
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto empty = enumerate_running_applications(cancelled.get_token());
    require(empty.applications.empty() && !empty.error, "cancelled enumeration still ran");
}

int process_target() {
    const auto instance = GetModuleHandleW(nullptr);
    const auto normal = CreateWindowW(L"STATIC", L"Isolated picker visible window",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 160, nullptr, nullptr, instance, nullptr);
    const auto minimized = CreateWindowW(L"STATIC", L"Isolated picker minimized window",
        WS_OVERLAPPEDWINDOW, 0, 0, 300, 160, nullptr, nullptr, instance, nullptr);
    const auto tool = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Excluded picker tool window",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 160, nullptr, nullptr, instance, nullptr);
    const auto hidden = CreateWindowW(L"STATIC", L"Excluded picker hidden window",
        WS_OVERLAPPEDWINDOW, 0, 0, 300, 160, nullptr, nullptr, instance, nullptr);
    if (!normal || !minimized || !tool || !hidden) return 1;
    ShowWindow(minimized, SW_SHOWMINNOACTIVE);
    MSG message{};
    while (IsWindow(normal) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

void native_enumeration_test() {
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --process-target";
    STARTUPINFOW startup{.cb = sizeof(startup)};
    PROCESS_INFORMATION child{};
    require(CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &child) != FALSE, "could not start isolated process target");
    CloseHandle(child.hThread);
    auto cleanup = [&] {
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid == static_cast<DWORD>(data)) PostMessageW(window, WM_CLOSE, 0, 0);
            return TRUE;
        }, static_cast<LPARAM>(child.dwProcessId));
        if (WaitForSingleObject(child.hProcess, 1500) == WAIT_TIMEOUT) {
            TerminateProcess(child.hProcess, 1);
            WaitForSingleObject(child.hProcess, 1500);
        }
        CloseHandle(child.hProcess);
    };
    try {
        WaitForInputIdle(child.hProcess, 3000);
        ApplicationSnapshot snapshot;
        const auto identity = normalize_path(exe);
        const auto end = GetTickCount64() + 3000;
        bool found = false;
        while (!found && GetTickCount64() < end) {
            snapshot = merge_running_applications(enumerate_running_applications({}));
            require(!snapshot.error, "native current-session enumeration failed");
            for (const auto& application : snapshot.applications) {
                if (normalize_path(application.executable_path) != identity) continue;
                if (!application.has_visible_window) continue;
                const auto titles = application_window_titles(application);
                if (titles.find(L"Isolated picker minimized window") == std::wstring::npos) continue;
                require(titles.find(L"Isolated picker visible window") != std::wstring::npos,
                    "multiple windows were not merged");
                require(titles.find(L"Excluded picker") == std::wstring::npos,
                    "hidden or tool window included");
                found = true;
            }
            if (!found) Sleep(20);
        }
        require(found, "isolated native process missing from snapshot");
        cleanup();
    } catch (...) {
        cleanup();
        throw;
    }
}

void source_menu_tests() {
    settings_page_test::Host host;
    Localization localization(UiLanguage::english);
    for (const auto source : {ApplicationSource::file, ApplicationSource::running, ApplicationSource::cancel}) {
        bool sent = false;
        Timer timer([&] {
            GUITHREADINFO state{.cbSize = sizeof(state)};
            if (sent || !GetGUIThreadInfo(GetCurrentThreadId(), &state)
                || !(state.flags & GUI_INMENUMODE)) return;
            sent = true;
            if (source == ApplicationSource::cancel) PostMessageW(host.window, WM_KEYDOWN, VK_ESCAPE, 0);
            else {
                PostMessageW(host.window, WM_KEYDOWN, VK_DOWN, 0);
                if (source == ApplicationSource::running) PostMessageW(host.window, WM_KEYDOWN, VK_DOWN, 0);
                PostMessageW(host.window, WM_KEYDOWN, VK_RETURN, 0);
            }
        });
        require(show_application_source_menu(host.window, host.window, localization) == source,
            "native source menu selection failed");
        timer.check();
    }
}

void picker_tests() {
    settings_page_test::Host host;
    UiDispatcher dispatcher(GetModuleHandleW(nullptr));
    for (auto language : {UiLanguage::simplified_chinese, UiLanguage::traditional_chinese, UiLanguage::english}) {
        Localization localization(language);
        std::atomic_int enumerations = 0;
        ApplicationSelectionServices services{
            .dispatcher = &dispatcher,
            .enumerate = [&](std::stop_token) { ++enumerations; return fixture(); }};
        int phase = 0;
        Timer timer([&] {
            const auto dialog = FindWindowW(picker_class, nullptr);
            if (!ready(dialog)) return;
            const auto list = GetDlgItem(dialog, 204);
            if (phase == 0) {
                require(ListView_GetItemCount(list) == 3, "picker did not default to visible applications");
                require(!IsWindowEnabled(GetDlgItem(dialog, IDOK)), "unselected picker can confirm");
                require(GetFocus() == GetDlgItem(dialog, 201), "search did not receive initial focus");
                const auto full_title = localization.text("settings.input_method.process_title");
                require(full_title != L"settings.input_method.process_title", "picker translation missing");
                for (UINT dpi : {96U, 144U, 192U}) {
                    RECT outer{0, 0, MulDiv(640, dpi, 96), MulDiv(420, dpi, 96)};
                    AdjustWindowRectExForDpi(&outer,
                        static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_STYLE)), FALSE,
                        static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_EXSTYLE)), dpi);
                    OffsetRect(&outer, -outer.left, -outer.top);
                    SendMessageW(dialog, WM_DPICHANGED, MAKEWPARAM(dpi, dpi),
                        reinterpret_cast<LPARAM>(&outer));
                    for (auto child = GetWindow(dialog, GW_CHILD); child;
                            child = GetWindow(child, GW_HWNDNEXT))
                        if (IsWindowVisible(child))
                            (void)settings_page_test::visible_bounds(dialog, child);
                }
                select_row(dialog, 1);
                require(IsWindowEnabled(GetDlgItem(dialog, IDOK)), "selection did not enable confirmation");
                SendMessageW(GetDlgItem(dialog, 202), BM_CLICK, 0, 0);
                require(!IsWindowEnabled(GetDlgItem(dialog, 202))
                    && !IsWindowEnabled(GetDlgItem(dialog, IDOK)), "refresh leaves controls enabled");
                phase = 1;
            } else if (phase == 1) {
                require(enumerations == 2, "refresh did not enumerate again");
                require(ListView_GetNextItem(list, -1, LVNI_SELECTED) == 1, "refresh lost path selection");
                SetWindowTextW(GetDlgItem(dialog, 201), L"SECOND DOCUMENT");
                require(ListView_GetItemCount(list) == 1, "merged title search failed");
                require(!IsWindowEnabled(GetDlgItem(dialog, IDOK)), "filtered-out selection remained enabled");
                SetWindowTextW(GetDlgItem(dialog, 201), L"");
                SendMessageW(GetDlgItem(dialog, 203), BM_CLICK, 0, 0);
                require(ListView_GetItemCount(list) == 4 && enumerations == 2,
                    "background filter reenumerated or omitted rows");
                select_row(dialog, 2);
                wchar_t path[1024]{};
                GetWindowTextW(GetDlgItem(dialog, 205), path, 1024);
                require(std::wstring(path) == L"C:\\Apps\\Worker.exe", "selected full path not shown");
                NMHDR notification{.hwndFrom = list, .idFrom = 204, .code = NM_DBLCLK};
                SendMessageW(dialog, WM_NOTIFY, 204, reinterpret_cast<LPARAM>(&notification));
                phase = 2;
            }
        });
        const auto chosen = show_running_application_dialog(GetModuleHandleW(nullptr),
            host.window, localization, services);
        timer.check();
        require(chosen == L"C:\\Apps\\Worker.exe" && phase == 2, "double click returned wrong path");
        require(IsWindowEnabled(host.window), "picker did not restore owner");
    }

    Localization localization(UiLanguage::english);
    for (int scenario = 0; scenario < 3; ++scenario) {
        ApplicationSelectionServices services{
            .dispatcher = &dispatcher,
            .enumerate = [scenario](std::stop_token) -> ApplicationSnapshot {
                if (scenario == 0) return {};
                if (scenario == 1) return {{}, ERROR_ACCESS_DENIED};
                return fixture();
            }};
        Timer timer([&] {
            const auto dialog = FindWindowW(picker_class, nullptr);
            if (!ready(dialog)) return;
            if (scenario == 2) SetWindowTextW(GetDlgItem(dialog, 201), L"nothing matches");
            require(ListView_GetItemCount(GetDlgItem(dialog, 204)) == 0, "empty or failed snapshot shows rows");
            require(!IsWindowEnabled(GetDlgItem(dialog, IDOK)), "empty picker confirms");
            if (scenario == 1)
                require(GetWindowTextLengthW(GetDlgItem(dialog, 206)) > 0, "failure not reported inline");
            PostMessageW(dialog, WM_KEYDOWN, VK_ESCAPE, 0);
        });
        require(!show_running_application_dialog(GetModuleHandleW(nullptr), host.window, localization, services),
            "Esc committed a selection");
        timer.check();
    }

    // Keyboard Enter must follow the native dialog's default button.
    ApplicationSelectionServices services{
        .dispatcher = &dispatcher, .enumerate = [](std::stop_token) { return fixture(); }};
    Timer enter_timer([&] {
        const auto dialog = FindWindowW(picker_class, nullptr);
        if (!ready(dialog)) return;
        select_row(dialog, 0);
        SetFocus(GetDlgItem(dialog, 204));
        PostMessageW(GetDlgItem(dialog, 204), WM_KEYDOWN, VK_RETURN, 0);
    });
    require(show_running_application_dialog(GetModuleHandleW(nullptr), host.window, localization, services)
        == L"C:\\Apps\\Editor.exe", "keyboard Enter did not confirm selected path");
    enter_timer.check();
}

void retry_and_scroll_tests() {
    settings_page_test::Host host;
    Localization localization(UiLanguage::english);
    UiDispatcher dispatcher(GetModuleHandleW(nullptr));
    std::atomic_int attempts = 0;
    ApplicationSelectionServices services{
        .dispatcher = &dispatcher,
        .enumerate = [&](std::stop_token) -> ApplicationSnapshot {
            if (++attempts == 1) throw std::runtime_error("simulated snapshot failure");
            ApplicationSnapshot result;
            for (int index = 0; index < 120; ++index)
                result.applications.push_back({L"C:\\Apps\\Editor" + std::to_wstring(index) + L".exe",
                    {L"A very long window title " + std::to_wstring(index)}, true});
            return result;
        }};
    int phase = 0;
    {
        Timer timer([&] {
            const auto dialog = FindWindowW(picker_class, nullptr);
            if (!ready(dialog)) return;
            const auto list = GetDlgItem(dialog, 204);
            if (phase == 0) {
                require(ListView_GetItemCount(list) == 0 && GetWindowTextLengthW(GetDlgItem(dialog, 206)) > 0,
                    "enumerator exception not reported");
                phase = 1;
                SendMessageW(GetDlgItem(dialog, 202), BM_CLICK, 0, 0);
            } else {
                require(attempts == 2 && ListView_GetItemCount(list) == 120, "retry did not recover snapshot");
                require(GetWindowLongPtrW(list, GWL_STYLE) & WS_VSCROLL, "dense picker not scrollable");
                ListView_EnsureVisible(list, 119, FALSE);
                require(ListView_GetTopIndex(list) > 0, "dense picker cannot scroll to last row");
                select_row(dialog, 119);
                SetFocus(GetDlgItem(dialog, 205));
                require(GetFocus() == GetDlgItem(dialog, 205), "full path not keyboard-focusable");
                phase = 2;
                SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            }
        });
        require(!show_running_application_dialog(GetModuleHandleW(nullptr), host.window, localization, services),
            "dense picker cancellation returned selection");
        timer.check();
    }
    require(phase == 2, "retry scenario incomplete");

    dispatcher.close();
    services.enumerate = [](std::stop_token) { return fixture(); };
    Timer closed_timer([&] {
        const auto dialog = FindWindowW(picker_class, nullptr);
        if (!ready(dialog)) return;
        require(GetWindowTextLengthW(GetDlgItem(dialog, 206)) > 0
            && !IsWindowEnabled(GetDlgItem(dialog, IDOK)), "rejected UI dispatch stuck or accepted stale result");
        SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    });
    require(!show_running_application_dialog(GetModuleHandleW(nullptr), host.window, localization, services),
        "closed dispatcher returned a selection");
    closed_timer.check();
}

void cancellation_tests() {
    settings_page_test::Host host;
    Localization localization(UiLanguage::english);
    UiDispatcher dispatcher(GetModuleHandleW(nullptr));
    std::atomic_bool started = false, stopped = false;
    ApplicationSelectionServices services{
        .dispatcher = &dispatcher,
        .enumerate = [&](std::stop_token stop) {
            started = true;
            while (!stop.stop_requested()) Sleep(2);
            stopped = true;
            return fixture();
        }};
    {
        Timer timer([&] {
            const auto dialog = FindWindowW(picker_class, nullptr);
            if (dialog && started) SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        });
        require(!show_running_application_dialog(GetModuleHandleW(nullptr), host.window, localization, services),
            "cancelled worker committed selection");
        timer.check();
    }
    require(stopped, "closing picker did not stop and join worker");

    // A worker can finish and queue its UI callback before the dialog closes.
    std::atomic_int finished = 0;
    services.enumerate = [&](std::stop_token) { ++finished; return fixture(); };
    {
        Timer timer([&] {
            const auto dialog = FindWindowW(picker_class, nullptr);
            if (!ready(dialog)) return;
            SendMessageW(GetDlgItem(dialog, 202), BM_CLICK, 0, 0);
            const auto end = GetTickCount64() + 1000;
            while (finished < 2 && GetTickCount64() < end) Sleep(1);
            require(finished == 2, "refresh worker did not finish while UI was busy");
            Sleep(20);
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        });
        (void)show_running_application_dialog(GetModuleHandleW(nullptr), host.window, localization, services);
        timer.check();
    }
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    require(!FindWindowW(picker_class, nullptr), "cancelled callback recreated picker");
}

void entry_tests() {
    settings_page_test::Host host;
    Localization localization(UiLanguage::english);
    UiDispatcher dispatcher(GetModuleHandleW(nullptr));
    Settings draft;
    std::vector<Profile> profiles;
    auto source = ApplicationSource::cancel;
    int file_calls = 0;
    ApplicationSelectionServices services{
        .dispatcher = &dispatcher,
        .enumerate = [](std::stop_token) { return fixture(); },
        .choose_source = [&](HWND, HWND, const Localization&) { return source; },
        .choose_file = [&](HWND, const Localization&) -> std::optional<std::wstring> {
            ++file_calls;
            return L"C:\\Apps\\FromFile.exe";
        }};
    auto page = make_settings_page(draft, {
        [&]() -> const auto& { return profiles; }, [] {}, [] { return Status{}; }, services});
    int changes = 0;
    page->create({GetModuleHandleW(nullptr), host.window, 96,
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), localization, [&] { ++changes; }});
    page->show(true);
    const auto window = FindWindowExW(host.window, nullptr, page_class, nullptr);
    SendMessageW(GetDlgItem(window, 2), BM_CLICK, 0, 0);
    require(changes == 0 && draft.rules.empty(), "source-menu cancel changed draft");
    for (int scenario = 0; scenario < 4; ++scenario) {
        source = scenario == 0 ? ApplicationSource::file : ApplicationSource::running;
        bool edited = false;
        Timer timer([&] {
            if (auto picker = FindWindowW(picker_class, nullptr)) {
                if (!ready(picker)) return;
                if (scenario == 1) SendMessageW(picker, WM_COMMAND, IDCANCEL, 0);
                else {
                    select_row(picker, 0);
                    SendMessageW(picker, WM_COMMAND, IDOK, 0);
                }
            } else if (auto rule = FindWindowW(rule_class, nullptr)) {
                require(changes == 0 && draft.rules.empty(), "picker committed before rule confirmation");
                edited = true;
                SendMessageW(rule, WM_COMMAND, scenario == 3 ? IDOK : IDCANCEL, 0);
            }
        });
        SendMessageW(GetDlgItem(window, 2), BM_CLICK, 0, 0);
        timer.check();
        require(edited == (scenario != 1), "picker did not follow rule-editor flow");
    }
    require(file_calls == 1 && changes == 1 && draft.rules.size() == 1 && !draft.enabled
        && draft.rules[0].executable_path == L"C:\\Apps\\Editor.exe",
        "confirmed add lost path, enabled module or changed draft count");

    Timer duplicate_timer([&] {
        if (auto picker = FindWindowW(picker_class, nullptr)) {
            if (!ready(picker)) return;
            select_row(picker, 0);
            SendMessageW(picker, WM_COMMAND, IDOK, 0);
        } else if (auto rule = FindWindowW(rule_class, nullptr)) {
            SendMessageW(rule, WM_COMMAND, IDOK, 0);
            require(IsWindow(rule) && GetWindowTextLengthW(GetDlgItem(rule, 115)) > 0,
                "running application duplicate bypassed existing validation");
            SendMessageW(rule, WM_COMMAND, IDCANCEL, 0);
        }
    });
    SendMessageW(GetDlgItem(window, 2), BM_CLICK, 0, 0);
    duplicate_timer.check();
    require(changes == 1 && draft.rules.size() == 1, "duplicate overwrote draft");
    page.reset();

    // Browsing an existing rule offers the identical sources but only updates
    // its local path until the rule itself is accepted.
    for (auto choice : {ApplicationSource::file, ApplicationSource::running}) {
        source = choice;
        bool opened = false, chosen = false;
        Timer browse_timer([&] {
            if (auto picker = FindWindowW(picker_class, nullptr)) {
                if (!ready(picker)) return;
                select_row(picker, 0);
                chosen = true;
                SendMessageW(picker, WM_COMMAND, IDOK, 0);
            } else if (auto rule = FindWindowW(rule_class, nullptr)) {
                if (!opened) {
                    opened = true;
                    SendMessageW(GetDlgItem(rule, 105), BM_CLICK, 0, 0);
                    if (choice == ApplicationSource::file) chosen = true;
                } else if (chosen) {
                    wchar_t path[1024]{};
                    GetWindowTextW(GetDlgItem(rule, 101), path, 1024);
                    require(std::wstring(path) == (choice == ApplicationSource::file
                        ? L"C:\\Apps\\FromFile.exe" : L"C:\\Apps\\Editor.exe"),
                        "rule path browsing did not use chosen source");
                    SendMessageW(rule, WM_COMMAND, IDCANCEL, 0);
                }
            }
        });
        require(!show_rule_dialog(GetModuleHandleW(nullptr), host.window, localization,
            {L"C:\\Apps\\Original.exe"}, profiles, {}, services), "cancelled browse committed rule");
        browse_timer.check();
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--process-target") return process_target();
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        model_tests();
        native_enumeration_test();
        source_menu_tests();
        picker_tests();
        retry_and_scroll_tests();
        cancellation_tests();
        entry_tests();
        std::cout << "running application model, picker, cancellation and entry tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
