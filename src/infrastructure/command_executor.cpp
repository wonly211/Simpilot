#include "simpilot/command_executor.hpp"
#include <shellapi.h>
#include <shlobj_core.h>
#include <tlhelp32.h>
#include <algorithm>
#include <vector>

namespace simpilot {
namespace {
std::vector<DWORD> matching_processes(const std::filesystem::path& executable) {
    std::vector<DWORD> result;
    std::error_code error;
    const auto target = std::filesystem::absolute(executable, error).wstring();
    if (target.empty()) return result;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{.dwSize = sizeof(entry)};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                             entry.th32ProcessID);
            if (!process) continue;
            std::wstring path(32768, L'\0');
            DWORD size = static_cast<DWORD>(path.size());
            if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
                path.resize(size);
                if (_wcsicmp(path.c_str(), target.c_str()) == 0) {
                    result.push_back(entry.th32ProcessID);
                }
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

struct WindowSearch {
    const std::vector<DWORD>* processes = nullptr;
    HWND result = nullptr;
};

BOOL CALLBACK find_process_window(const HWND window, const LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (std::ranges::find(*search->processes, process) == search->processes->end()) return TRUE;
    if (GetWindow(window, GW_OWNER) != nullptr) return TRUE;
    search->result = window;
    return FALSE;
}

HWND find_process_window(const std::vector<DWORD>& processes) {
    WindowSearch search{&processes, nullptr};
    EnumWindows(&find_process_window, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

int show_command(const LaunchVisibility visibility) noexcept {
    switch (visibility) {
    case LaunchVisibility::minimized: return SW_SHOWMINIMIZED;
    case LaunchVisibility::maximized: return SW_SHOWMAXIMIZED;
    case LaunchVisibility::hidden: return SW_HIDE;
    case LaunchVisibility::normal: default: return SW_SHOWNORMAL;
    }
}

} // namespace
std::wstring user_profile_directory() {
    PWSTR profile = nullptr;
    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &profile))) {
        return {};
    }
    std::wstring result;
    try {
        result = profile;
    } catch (...) {
        CoTaskMemFree(profile);
        throw;
    }
    CoTaskMemFree(profile);
    return result;
}

void execute_command(HWND owner, const LaunchRequest& request, const LaunchErrorSink& error) {
    if (request.application && request.existing_process_action != ExistingProcessAction::start_new_instance) {
        const auto processes = matching_processes(request.target);
        if (!processes.empty()) {
            if (request.existing_process_action == ExistingProcessAction::do_nothing) return;
            if (const auto target = find_process_window(processes)) {
                ShowWindow(target, IsIconic(target) ? SW_RESTORE : SW_SHOW);
                BringWindowToTop(target);
                SetForegroundWindow(target);
                return;
            }
        }
    }
    SHELLEXECUTEINFOW execution{
        .cbSize = sizeof(execution),
        .fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI,
        .hwnd = owner,
        .lpVerb = request.application && request.run_as_administrator ? L"runas" : L"open",
        .lpFile = request.target.c_str(),
        .lpParameters = request.application && !request.arguments.empty() ? request.arguments.c_str() : nullptr,
        .lpDirectory = request.application && !request.working_directory.empty() ? request.working_directory.c_str() : nullptr,
        .nShow = request.application ? show_command(request.visibility) : SW_SHOWNORMAL,
    };
    if (!ShellExecuteExW(&execution)) {
        if (error) error(owner, request.target, GetLastError());
        return;
    }
    if (execution.hProcess) CloseHandle(execution.hProcess);
}
} // namespace simpilot
