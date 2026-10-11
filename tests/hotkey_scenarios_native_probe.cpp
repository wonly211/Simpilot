// Isolated Windows acceptance. Sends replay-tagged F22/F23/F24 through the
// production hook; no user settings, user targets, singleton or physical Win input.
#include "keyboard_manager.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace simpilot;
KeyboardManager keyboard;
std::array<unsigned, 8> actions{};
bool deferred = false;
int deferred_identifier = 0;
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_HOTKEY) {
        if (deferred) deferred_identifier = static_cast<int>(wparam);
        else if (wparam < actions.size() && keyboard.activation_allows_dispatch(static_cast<int>(wparam))) ++actions[wparam];
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void pump(unsigned milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(5);
    } while (GetTickCount64() < end);
}
void key(UINT virtual_key, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(virtual_key);
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    input.ki.dwExtraInfo = keyboard_replay_injected_marker;
    require(SendInput(1, &input, sizeof(input)) == 1, "Tagged keyboard replay unavailable");
    pump(120);
}
void tap(UINT virtual_key) { key(virtual_key, true); key(virtual_key, false); }
struct Child {
    HANDLE process = nullptr;
    DWORD identifier = 0;
    HWND window = nullptr;
    explicit Child(const std::filesystem::path& executable) {
        auto command = L"\"" + executable.wstring() + L"\" --child";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION info{};
        require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &info) != FALSE, "Start isolated scene process");
        process = info.hProcess; identifier = info.dwProcessId; CloseHandle(info.hThread);
        const auto deadline = GetTickCount64() + 5000;
        while (!window && GetTickCount64() < deadline) {
            EnumWindows([](HWND candidate, LPARAM data) -> BOOL {
                auto& child = *reinterpret_cast<Child*>(data);
                DWORD identifier = 0;
                GetWindowThreadProcessId(candidate, &identifier);
                if (identifier == child.identifier && IsWindowVisible(candidate)) { child.window = candidate; return FALSE; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(this));
            pump(20);
        }
        if (!window) { stop(); throw std::runtime_error("Isolated scene window is not ready"); }
    }
    void stop() {
        if (!process) return;
        TerminateProcess(process, 0); WaitForSingleObject(process, 5000); CloseHandle(process);
        process = nullptr; window = nullptr;
    }
    ~Child() { stop(); }
};
void focus(HWND window) {
    SetForegroundWindow(window); pump(100);
    require(GetForegroundWindow() == window, "Desktop does not permit isolated foreground acceptance");
}
}

int wmain(int argc, wchar_t**) {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{.lpfnWndProc = procedure, .hInstance = instance,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW), .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = L"Simpilot.IsolatedScenarioProbe"};
    RegisterClassW(&type);
    const auto window = CreateWindowW(type.lpszClassName, L"Simpilot isolated scene acceptance",
        WS_OVERLAPPEDWINDOW, 40, 40, 560, 240, nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    if (argc > 1) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
        return 0;
    }
    const auto previous_foreground = GetForegroundWindow();
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    const auto root = std::filesystem::temp_directory_path() /
        (L"Simpilot-native-scenes-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root / L"a"); std::filesystem::create_directories(root / L"b");
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); } } cleanup{root};
    const auto a_path = root / L"a" / L"scene.exe", b_path = root / L"b" / L"scene.exe";
    std::filesystem::copy_file(executable, a_path); std::filesystem::copy_file(executable, b_path);
    int result = 0;
    try {
        ShowWindow(window, SW_SHOW); focus(window);
        require(keyboard.start(window), "Start product hook");
        HotKeyBinding binding{HotKeyGesture{0, VK_F24}, true};
        binding.activation.require_running_program = true;
        binding.activation.programs = {a_path.wstring()}; binding.activation.priority = 10;
        require(keyboard.register_binding(1, binding), "Register first background scene");
        binding.activation.programs = {b_path.wstring()}; binding.activation.priority = 20;
        require(keyboard.register_binding(2, binding), "Register overlapping higher-priority scene");
        binding.activation = {}; binding.activation.priority = 0;
        require(keyboard.register_binding(3, binding), "Register explicit default");
        tap(VK_F24); require(actions[3] == 1 && !actions[1] && !actions[2], "Only explicit default runs without scenes");
        Child a(a_path); focus(window); pump(700);
        tap(VK_F24); require(actions[1] == 1, "Background scene activates");
        Child b(b_path); focus(window); pump(700);
        tap(VK_F24); require(actions[2] == 1 && actions[1] == 1, "Both running selects only higher priority");
        b.stop(); tap(VK_F24); require(actions[1] == 2, "Process exit falls back to the surviving scene");
        key(VK_F24, true); require(actions[1] == 3, "First down chooses lower scene");
        Child restarted_b(b_path); focus(window); pump(700);
        key(VK_F24, true); key(VK_F24, false);
        require(actions[1] == 3 && actions[2] == 1, "Repeat does not change selected scene after conditions change");
        tap(VK_F24); require(actions[2] == 2, "Next complete press selects new higher scene");
        binding.gesture = HotKeyGesture{0, VK_F23}; binding.activation = {};
        binding.activation.require_foreground_program = true; binding.activation.programs = {executable};
        require(keyboard.register_binding(4, binding), "Register host foreground scene");
        binding.activation.programs = {a_path.wstring()};
        require(keyboard.register_binding(5, binding), "Different foreground path may reuse the same key");
        pump(700); focus(window); tap(VK_F23); require(actions[4] == 1 && !actions[5], "Host foreground selects host rule");
        focus(a.window); tap(VK_F23); require(actions[5] == 1 && actions[4] == 1, "Switching foreground selects associated process");
        focus(window);
        binding.gesture = HotKeyGesture{0, VK_F22}; binding.activation = {};
        binding.activation.require_running_program = true; binding.activation.programs = {b_path.wstring()};
        require(keyboard.register_binding(6, binding), "Register exact-instance dispatch gate");
        pump(700); deferred = true; tap(VK_F22);
        require(deferred_identifier == 6, "Action was queued with a process condition");
        restarted_b.stop(); Child replacement_b(b_path); focus(window); pump(700);
        tap(VK_F22); // A second complete press queues a different process instance.
        require(!keyboard.activation_allows_dispatch(6), "New instance cannot revive queued action selected for an exited process");
        require(keyboard.activation_allows_dispatch(6), "Each queued press retains its own live process instance");
        require(!keyboard.activation_allows_dispatch(6), "A queued decision can only be dispatched once");
        deferred = false; tap(VK_F22); require(actions[6] == 1, "Next press recognizes replacement instance");
        keyboard.unregister_all();
        std::cout << "PASS: explicit fallback, two background scenes, precedence, held-key stability, foreground switching and exact-instance dispatch gate.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    keyboard.stop(); DestroyWindow(window);
    if (IsWindow(previous_foreground)) SetForegroundWindow(previous_foreground);
    return result;
}
