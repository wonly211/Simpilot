// Interactive acceptance harness: no user settings, shell actions or app singleton.
// F24 routes by Num Lock, F23 requires the isolated child, F22 requires Caps+Scroll.
#include "keyboard_manager.hpp"
#include <filesystem>
#include <iostream>

namespace {
simpilot::KeyboardManager keyboard;
HWND summary = nullptr;
HANDLE child = nullptr;
std::filesystem::path folder, child_path;
std::array<int, 4> invoked{};
std::array<int, 3> passed_down{}, passed_up{};
std::array<bool, 3> initial_locks{};
constexpr UINT locks[]{VK_CAPITAL, VK_NUMLOCK, VK_SCROLL};

void update() {
    std::wstring text = L"ISOLATED HOTKEY ACCEPTANCE PROBE\r\n\r\n"
        L"F24: Num Lock on/off -> action 1/2\r\n"
        L"F23: requires the test background process\r\n"
        L"F22: requires Caps Lock AND Scroll Lock on\r\n\r\n";
    for (unsigned i = 0; i < 4; ++i) text += L"Action " + std::to_wstring(i + 1) + L": " + std::to_wstring(invoked[i]) + L"\r\n";
    text += L"\r\nPassed keydown / keyup (F22, F23, F24):\r\n";
    for (unsigned i = 0; i < 3; ++i) text += std::to_wstring(passed_down[i]) + L" / " + std::to_wstring(passed_up[i]) + L"    ";
    text += child ? L"\r\n\r\nTest child: running" : L"\r\n\r\nTest child: stopped";
    text += L"\r\nCaps / Num / Scroll: ";
    for (const auto key : locks) text += (GetKeyState(key) & 1) ? L"ON  " : L"OFF  ";
    SetWindowTextW(summary, text.c_str());
}
void stop_child() {
    if (!child) return;
    TerminateProcess(child, 0);
    WaitForSingleObject(child, 5000);
    CloseHandle(child);
    child = nullptr;
}
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == 1 && !child) {
            auto command = L"\"" + child_path.wstring() + L"\" --child";
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION process{};
            if (CreateProcessW(child_path.c_str(), command.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
                child = process.hProcess;
                CloseHandle(process.hThread);
            }
        }
        if (LOWORD(wparam) == 2) stop_child();
        SetFocus(window);
        if (LOWORD(wparam) >= 10 && LOWORD(wparam) <= 12) {
            INPUT events[2]{};
            for (auto& input : events) { input.type = INPUT_KEYBOARD;
                input.ki.wVk = static_cast<WORD>(VK_F22 + LOWORD(wparam) - 10);
                input.ki.dwExtraInfo = simpilot::keyboard_replay_injected_marker; }
            events[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, events, sizeof(INPUT));
        }
        update();
        return 0;
    }
    if (message == WM_HOTKEY) {
        if (wparam >= 1 && wparam <= 4 && keyboard.activation_allows_dispatch(static_cast<int>(wparam)))
            ++invoked[wparam - 1];
        update();
        return 0;
    }
    if (message == WM_KEYDOWN || message == WM_KEYUP) {
        if (wparam >= VK_F22 && wparam <= VK_F24)
            ++(message == WM_KEYDOWN ? passed_down : passed_up)[wparam - VK_F22];
        update();
    }
    if (message == WM_TIMER) { update(); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
}
int wmain(int argc, wchar_t**) {
    if (argc > 1) { Sleep(120000); return 0; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    for (unsigned i = 0; i < 3; ++i) initial_locks[i] = (GetKeyState(locks[i]) & 1) != 0;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    folder = std::filesystem::temp_directory_path() /
        (L"Simpilot-native-hotkey-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directory(folder);
    child_path = folder / L"SimpilotConditionBackground.exe";
    std::filesystem::copy_file(executable, child_path);
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{.lpfnWndProc = procedure, .hInstance = instance,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW), .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = L"Simpilot.HotkeyConditionProbe"};
    RegisterClassW(&type);
    const auto window = CreateWindowW(type.lpszClassName, L"Simpilot isolated hotkey acceptance",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 690, nullptr, nullptr, instance, nullptr);
    summary = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 24, 24, 680, 430, window, nullptr, instance, nullptr);
    SendMessageW(summary, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    CreateWindowW(L"BUTTON", L"Start test background process", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        24, 475, 260, 36, window, reinterpret_cast<HMENU>(1), instance, nullptr);
    CreateWindowW(L"BUTTON", L"Stop test background process", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        300, 475, 260, 36, window, reinterpret_cast<HMENU>(2), instance, nullptr);
    for (int i = 0; i < 3; ++i) {
        const auto label = L"Send tagged F" + std::to_wstring(22 + i);
        CreateWindowW(L"BUTTON", label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            24 + 190 * i, 530, 180, 36, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(10 + i)), instance, nullptr);
    }
    if (!keyboard.start(window)) return 2;
    simpilot::HotKeyBinding binding{simpilot::HotKeyGesture{0, VK_F24}, false};
    binding.activation.locks[1] = simpilot::ToggleRequirement::on;
    if (!keyboard.register_binding(1, binding)) return 3;
    binding.activation.locks[1] = simpilot::ToggleRequirement::off;
    if (!keyboard.register_binding(2, binding)) return 4;
    binding.gesture->virtual_key = VK_F23;
    binding.activation.locks = {};
    binding.activation.require_running_program = true;
    binding.activation.programs = {child_path.wstring()};
    if (!keyboard.register_binding(3, binding)) return 5;
    binding.gesture->virtual_key = VK_F22;
    binding.activation = {};
    binding.activation.locks[0] = binding.activation.locks[2] = simpilot::ToggleRequirement::on;
    if (!keyboard.register_binding(4, binding)) return 6;
    update();
    SetTimer(window, 1, 500, nullptr);
    ShowWindow(window, SW_SHOW);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    keyboard.stop();
    stop_child();
    for (unsigned i = 0; i < 3; ++i) {
        if (((GetKeyState(locks[i]) & 1) != 0) == initial_locks[i]) continue;
        INPUT events[2]{};
        for (auto& input : events) { input.type = INPUT_KEYBOARD; input.ki.wVk = static_cast<WORD>(locks[i]);
            input.ki.dwExtraInfo = simpilot::keyboard_target_injected_marker; }
        events[0].ki.dwFlags = locks[i] == VK_NUMLOCK ? KEYEVENTF_EXTENDEDKEY : 0;
        events[1].ki.dwFlags = events[0].ki.dwFlags | KEYEVENTF_KEYUP;
        SendInput(2, events, sizeof(INPUT));
    }
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    for (const auto count : invoked) std::cout << count << ' ';
    std::cout << '\n';
}
