#include "tray_application.hpp"

#include <Windows.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path current_executable_path() {
    std::wstring value(MAX_PATH, L'\0');
    while (true) {
        const auto written = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
        if (written == 0) {
            throw std::runtime_error("Unable to determine executable path");
        }
        if (written < value.size() - 1) {
            value.resize(written);
            return value;
        }
        value.resize(value.size() * 2);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const auto executable = current_executable_path();
    HANDLE single_instance = CreateMutexW(nullptr, TRUE, L"Local\\Simpilot");
    if (!single_instance) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (const auto existing = FindWindowExW(
                    HWND_MESSAGE, nullptr, simpilot::tray_window_class_name, nullptr)) {
                PostMessageW(existing, simpilot::show_main_menu_message, 0, 0);
                break;
            }
            Sleep(50);
        }
        CloseHandle(single_instance);
        return 0;
    }

    simpilot::TrayApplication application(instance, executable);
    const auto result = application.run();
    CloseHandle(single_instance);
    return result;
}
