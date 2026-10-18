#include "tray_application.hpp"
#include "configuration_backup.hpp"

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

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const auto executable = current_executable_path();
    const std::wstring arguments(command_line ? command_line : L"");
    if (arguments.starts_with(L"--restore-wait ")) {
        try {
            const auto process_id = std::stoul(arguments.substr(15));
            if (const auto process = OpenProcess(SYNCHRONIZE, FALSE, process_id)) {
                const auto waited = WaitForSingleObject(process, 60000);
                CloseHandle(process);
                if (waited != WAIT_OBJECT_0) return 1;
            }
        } catch (...) { return 1; }
    }
    HANDLE single_instance = CreateMutexW(nullptr, TRUE, L"Local\\Simpilot");
    if (!single_instance) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (const auto existing = FindWindowExW(
                    HWND_MESSAGE, nullptr, simpilot::tray_window_class_name, nullptr)) {
                PostMessageW(existing, simpilot::activate_primary_message, 0, 0);
                break;
            }
            Sleep(50);
        }
        CloseHandle(single_instance);
        return 0;
    }

    int result = 1;
    if (simpilot::prepare_configuration(instance, executable)) {
        simpilot::TrayApplication application(instance, executable);
        result = application.run();
    }
    if (result == simpilot::restore_restart_exit_code && !simpilot::restart_for_restore(executable)) {
        const simpilot::Localization localization(GetUserDefaultUILanguage() == 0x0409 ? "en-US" : "zh-CN");
        MessageBoxW(nullptr, localization.text("backup.restart_failed").data(), L"Simpilot", MB_OK | MB_ICONERROR);
    }
    CloseHandle(single_instance);
    return result;
}
