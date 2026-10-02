#include "running_applications.hpp"

#include "input_method_model.hpp"

#include <dwmapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <utility>

namespace simpilot::input_method {
namespace {

struct HandleCloser {
    void operator()(void* handle) const noexcept {
        if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

std::wstring fold(std::wstring_view value) {
    if (value.empty()) return {};
    const auto size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!size) return std::wstring(value);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
            value.data(), static_cast<int>(value.size()), result.data(), size,
            nullptr, nullptr, 0)) return std::wstring(value);
    return result;
}

struct WindowSnapshot {
    DWORD session = 0;
    DWORD self = 0;
    std::stop_token stop;
    std::map<DWORD, std::vector<std::wstring>> titles;
    bool failed = false;
};

BOOL CALLBACK collect_window(HWND window, LPARAM data) noexcept {
    auto& state = *reinterpret_cast<WindowSnapshot*>(data);
    if (state.stop.stop_requested()) return FALSE;
    try {
        DWORD pid = 0, session = 0;
        GetWindowThreadProcessId(window, &pid);
        if (!pid || pid == state.self || !ProcessIdToSessionId(pid, &session)
            || session != state.session || !IsWindowVisible(window)
            || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
            && cloaked) return TRUE;
        // Cross-process top-level captions do not send synchronous messages
        // to the target's UI thread. Never query our own windows here.
        std::wstring title(32768, L'\0');
        const auto length = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
        title.resize(static_cast<std::size_t>(std::max(0, length)));
        state.titles[pid].push_back(std::move(title));
    } catch (...) {
        state.failed = true;
        return FALSE;
    }
    return TRUE;
}

} // namespace

ApplicationSnapshot enumerate_running_applications(std::stop_token stop) {
    ApplicationSnapshot result;
    if (stop.stop_requested()) return result;
    DWORD session = 0;
    const auto self = GetCurrentProcessId();
    if (!ProcessIdToSessionId(self, &session)) {
        result.error = GetLastError();
        return result;
    }
    WindowSnapshot windows{session, self, stop};
    if (!EnumWindows(collect_window, reinterpret_cast<LPARAM>(&windows))) {
        if (stop.stop_requested()) return {};
        result.error = windows.failed ? ERROR_NOT_ENOUGH_MEMORY : GetLastError();
        if (!result.error) result.error = ERROR_GEN_FAILURE;
        return result;
    }
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        result.error = GetLastError();
        return result;
    }
    PROCESSENTRY32W entry{.dwSize = sizeof(entry)};
    if (!Process32FirstW(snapshot.get(), &entry)) {
        const auto error = GetLastError();
        if (error != ERROR_NO_MORE_FILES) result.error = error;
        return result;
    }
    do {
        if (stop.stop_requested()) return {};
        DWORD process_session = 0;
        if (entry.th32ProcessID == self
            || !ProcessIdToSessionId(entry.th32ProcessID, &process_session)
            || process_session != session) continue;
        UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
            entry.th32ProcessID));
        if (!process) continue;
        std::wstring path(32768, L'\0');
        DWORD length = static_cast<DWORD>(path.size());
        if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length)) continue;
        path.resize(length);
        const auto found = windows.titles.find(entry.th32ProcessID);
        result.applications.push_back({std::move(path),
            found == windows.titles.end() ? std::vector<std::wstring>{} : found->second,
            found != windows.titles.end()});
    } while (Process32NextW(snapshot.get(), &entry));
    const auto error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
        result.applications.clear();
        result.error = error;
    }
    return result;
}

ApplicationSnapshot merge_running_applications(ApplicationSnapshot snapshot) {
    std::map<std::wstring, RunningApplication> merged;
    for (auto& application : snapshot.applications) {
        auto identity = normalize_path(application.executable_path);
        if (identity.empty()) continue;
        auto [found, inserted] = merged.try_emplace(std::move(identity));
        auto& target = found->second;
        if (inserted) target.executable_path = std::move(application.executable_path);
        target.has_visible_window |= application.has_visible_window;
        for (auto& title : application.window_titles) {
            if (!title.empty() && std::ranges::find(target.window_titles, title)
                == target.window_titles.end()) target.window_titles.push_back(std::move(title));
        }
    }
    snapshot.applications.clear();
    for (auto& [identity, application] : merged) {
        (void)identity;
        snapshot.applications.push_back(std::move(application));
    }
    std::ranges::sort(snapshot.applications, [](const auto& left, const auto& right) {
        const auto left_name = fold(std::filesystem::path(left.executable_path).filename().wstring());
        const auto right_name = fold(std::filesystem::path(right.executable_path).filename().wstring());
        return left_name == right_name
            ? normalize_path(left.executable_path) < normalize_path(right.executable_path)
            : left_name < right_name;
    });
    return snapshot;
}

std::vector<std::size_t> filter_running_applications(
    const ApplicationSnapshot& snapshot, bool include_background, std::wstring_view search) {
    const auto query = fold(search);
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < snapshot.applications.size(); ++index) {
        const auto& application = snapshot.applications[index];
        if (!include_background && !application.has_visible_window) continue;
        if (query.empty() || fold(application.executable_path).find(query) != std::wstring::npos
            || std::ranges::any_of(application.window_titles, [&](const auto& title) {
                return fold(title).find(query) != std::wstring::npos;
            })) result.push_back(index);
    }
    return result;
}

std::wstring application_window_titles(const RunningApplication& application) {
    std::wstring result;
    for (const auto& title : application.window_titles) {
        if (!result.empty()) result += L" | ";
        result += title;
    }
    return result;
}

} // namespace simpilot::input_method
