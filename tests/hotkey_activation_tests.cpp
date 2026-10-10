#include "conditional_hotkey_state.hpp"
#include "hotkey_process_monitor.hpp"
#include "simpilot/hotkey_registry.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace simpilot;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void condition_tests() {
    std::vector<HotkeyActivationConditions> all;
    for (unsigned encoded = 0; encoded < 27; ++encoded) {
        HotkeyActivationConditions condition;
        auto number = encoded;
        for (auto& lock : condition.locks) { lock = static_cast<ToggleRequirement>(number % 3); number /= 3; }
        for (unsigned state = 0; state < 8; ++state) {
            bool expected = true;
            for (unsigned key = 0; key < 3; ++key) {
                if (condition.locks[key] == ToggleRequirement::on && !(state & (1U << key))) expected = false;
                if (condition.locks[key] == ToggleRequirement::off && (state & (1U << key))) expected = false;
            }
            require(bool(condition.allowed_lock_states() & (1U << state)) == expected, "Lock-state truth table");
        }
        all.push_back(condition);
    }
    for (const auto& a : all) for (const auto& b : all) {
        bool overlap = false;
        for (unsigned state = 0; state < 8; ++state)
            overlap |= (a.allowed_lock_states() & (1U << state)) && (b.allowed_lock_states() & (1U << state));
        require(activation_overlaps(a, b) == overlap, "All condition intersections");
    }
    HotkeyActivationConditions a, b;
    a.require_running_program = b.require_running_program = true;
    a.programs = {L"C:\\A\\editor.exe"};
    b.programs = {L"C:\\B\\editor.exe"};
    require(activation_overlaps(a, b), "Different running programs are not exclusive");
    a.programs.clear();
    require(!a.valid(), "Enabled empty program list rejected");
    a.programs = {L"C:\\A\\editor.exe", L"c:/a/EDITOR.exe"};
    require(!a.valid(), "Duplicate normalized paths rejected");
    a.programs = {L"relative.exe"};
    require(!a.valid(), "Relative process identities rejected");
    for (const auto path : {L"C:\\Apps\\file:stream.exe", L"C:\\Apps\\invalid\t.exe",
                            L"C:\\Apps\\*.exe"}) {
        a.programs = {path};
        require(!a.valid(), "Invalid Windows program path rejected");
    }
    a.programs.clear();
    for (int i = 0; i < 33; ++i) a.programs.push_back(L"C:\\" + std::to_wstring(i) + L".exe");
    require(!a.valid(), "Program list limit");
    require(normalize_hotkey_program_path(L"\\\\?\\C:\\Apps\\..\\Apps\\EDITOR.exe")
        == normalize_hotkey_program_path(L"c:/apps/editor.exe"), "Absolute path normalization");
}

void press_tests() {
    ConditionalHotkeyState state;
    HotkeyActivationConditions on, off;
    on.locks[1] = ToggleRequirement::on;
    off.locks[1] = ToggleRequirement::off;
    state.registrations = {{1, {MOD_CONTROL, VK_F24}, on}, {2, {MOD_CONTROL, VK_F24}, off}};
    const auto process = [](const auto&) noexcept { return true; };
    require(state.handle(VK_F24, true, MOD_CONTROL, 2, process).invoke == 1, "Num Lock on selects first action");
    auto repeat = state.handle(VK_F24, true, 0, 0, process);
    require(!repeat.invoke && repeat.consume, "Repeat retains ownership after conditions and modifiers change");
    require(state.handle(VK_F24, false, 0, 0, process).consume, "Consumed press owns release");
    require(state.handle(VK_F24, true, MOD_CONTROL, 0, process).invoke == 2, "Num Lock off selects second action");
    state.clear_registrations();
    require(state.handle(VK_F24, false, 0, 0, process).consume, "Unregister while held still consumes release");
    state.registrations = {{1, {MOD_CONTROL, VK_F24}, on}};
    require(!state.handle(VK_F24, true, MOD_CONTROL, 0, process).consume, "Unmatched down passes");
    require(!state.handle(VK_F24, true, MOD_CONTROL, 2, process).consume, "Passed press cannot activate mid-hold");
    require(!state.handle(VK_F24, false, 0, 2, process).consume, "Passed down always passes up");
    require(!state.handle(VK_F24, true, MOD_CONTROL, {}, process).consume, "Unknown lock state fails closed");
    (void)state.handle(VK_F24, false, 0, {}, process);
    on.require_running_program = true;
    on.programs = {L"C:\\test.exe"};
    state.registrations = {{1, {0, VK_NUMLOCK}, on}};
    require(!state.handle(VK_NUMLOCK, true, 0, 2, [](const auto&) noexcept { return false; }).consume,
            "Both lock and program conditions must match");
    (void)state.handle(VK_NUMLOCK, false, 0, 2, process);
    require(state.handle(VK_NUMLOCK, true, 0, 2, process).invoke == 1, "Lock trigger uses pre-press state");
    state.reset();
    on.locks = {};
    state.registrations = {{1, {0, VK_F24}, on}};
    require(state.handle(VK_F24, true, 0, {}, process).invoke == 1, "Program-only condition does not need lock state");
    (void)state.handle(VK_F24, false, 0, {}, process);
    state.captured(VK_F24, true);
    require(!state.handle(VK_F24, true, 0, {}, process).invoke,
        "Recording or mapping cannot become a conditional hotkey mid-hold");
    require(!state.handle(VK_F24, false, 0, {}, process).consume,
        "Previously captured press does not own an unrelated release");
    require(state.handle(VK_F24, true, 0, {}, process).invoke == 1,
        "The next full press can activate after capture ends");
    state.captured(VK_F24, false);
    require(state.handle(VK_F24, true, 0, {}, process).invoke == 1,
        "Resynchronizing a release missed during sleep permits the next full press");
    state.captured(VK_F24, true);
    require(!state.handle(VK_F24, true, 0, {}, process).invoke
        && state.handle(VK_F24, false, 0, {}, process).consume,
        "A still-held consumed key keeps ownership when resynchronized");
}

void registry_tests() {
    HotkeyRegistry registry;
    BuiltInHotKey a{{HotKeyGesture{MOD_CONTROL, VK_F24}, false}, true}, b = a;
    a.binding.activation.locks[1] = ToggleRequirement::on;
    b.binding.activation.locks[1] = ToggleRequirement::off;
    auto first = registry.add("on", 0, {"on", [&] { return a; }, [](HWND) {}, [&] { return &a; }});
    auto second = registry.add("off", 1, {"off", [&] { return b; }, [](HWND) {}, [&] { return &b; }});
    int count = 0, conflicts = 0;
    registry.refresh([&](int, const auto&) { ++count; return true; }, [&](auto) { ++conflicts; });
    require(count == 2 && !conflicts, "Disjoint rules both register");
    bool committed = false;
    require(registry.replace_conflicts("off", b, [&](const auto&) { ++conflicts; return false; },
        [&] { committed = true; }), "Mutually exclusive draft needs no replacement");
    require(committed && !conflicts, "No false conflict prompt");
    b.binding.activation = {};
    require(!registry.replace_conflicts("off", b, [](const auto&) { return false; }, [] {}),
            "Removing a condition detects the newly introduced overlap");
    a.binding.gesture = HotKeyGesture{MOD_WIN, L'S'};
    require(!registry.requires_windows_blocking(L'S' - L'A'), "Conditional Win binding does not force permanent blocking");
}

struct Child {
    HANDLE handle = nullptr;
    explicit Child(const std::filesystem::path& path) {
        auto command = L"\"" + path.wstring() + L"\" --background-child";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION info{};
        require(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &info) != FALSE, "Create isolated background process");
        handle = info.hProcess;
        CloseHandle(info.hThread);
    }
    void stop() { if (handle) { TerminateProcess(handle, 0); WaitForSingleObject(handle, 5000); CloseHandle(handle); handle = nullptr; } }
    ~Child() { stop(); }
};

void process_tests() {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    const auto root = std::filesystem::temp_directory_path() /
        (L"Simpilot-hotkey-process-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root / L"a");
    std::filesystem::create_directories(root / L"b");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{root};
    const auto path_a = root / L"a" / L"same.exe", path_b = root / L"b" / L"same.exe";
    std::filesystem::copy_file(executable, path_a);
    std::filesystem::copy_file(executable, path_b);
    const std::vector<std::wstring> a{normalize_hotkey_program_path(path_a.wstring())};
    const std::vector<std::wstring> b{normalize_hotkey_program_path(path_b.wstring())};
    Child child_a(path_a), second_a(path_a), child_b(path_b);
    auto snapshot = HotkeyProcessMonitor::enumerate(a);
    require(snapshot->valid && snapshot->processes.size() == 2 && snapshot->matches(a), "No-window multiple instances detected");
    require(!snapshot->matches(b), "Same name in another directory does not match");
    HotkeyProcessMonitor monitor;
    monitor.watch(a);
    std::unique_ptr<HotkeyProcessSnapshot> published;
    const auto deadline = GetTickCount64() + 10000;
    while (!published && GetTickCount64() < deadline) { published = monitor.take_snapshot(); Sleep(10); }
    require(published && published->matches(a), "Worker performs initial enumeration");
    child_a.stop();
    require(snapshot->matches(a), "One surviving instance keeps the condition active");
    second_a.stop();
    require(!snapshot->matches(a), "Exited processes cannot activate stale snapshot");
    Child restarted(path_a);
    require(!snapshot->matches(a), "Restart cannot revive old process handles");
    auto restarted_snapshot = HotkeyProcessMonitor::enumerate(a);
    require(restarted_snapshot->matches(a), "New process instance detected");
    require(!restarted_snapshot->matches(a, restarted_snapshot->captured + 1501), "Stalled snapshot expires");
    restarted_snapshot->valid = false;
    require(!restarted_snapshot->matches(a), "Failed enumeration cannot activate");
    monitor.stop();
    require(!monitor.take_snapshot(), "Stopped worker clears mailbox");
}
}

int wmain(int argc, wchar_t**) {
    if (argc > 1) { Sleep(30000); return 0; }
    try {
        condition_tests(); press_tests(); registry_tests(); process_tests();
        std::cout << "Hotkey activation behavior and native process tests passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
