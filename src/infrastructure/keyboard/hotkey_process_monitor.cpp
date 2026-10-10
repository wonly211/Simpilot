#include "hotkey_process_monitor.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>

namespace simpilot {
namespace {
struct CloseHandleDeleter {
    void operator()(void* handle) const noexcept {
        if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
};
using OwnedHandle = std::unique_ptr<void, CloseHandleDeleter>;
}

bool HotkeyProcessSnapshot::matches(const std::vector<std::wstring>& paths,
                                    ULONGLONG now) const noexcept {
    // A stalled/failed monitor must not leave indefinitely active conditions.
    if (!valid || now < captured || now - captured > 1500) return false;
    for (const auto& process : processes) {
        if (std::ranges::find(paths, process->path) != paths.end()
            && WaitForSingleObject(process->handle, 0) == WAIT_TIMEOUT) return true;
    }
    return false;
}

std::unique_ptr<HotkeyProcessSnapshot> HotkeyProcessMonitor::enumerate(
    const std::vector<std::wstring>& paths, std::stop_token stop) {
    auto result = std::make_unique<HotkeyProcessSnapshot>();
    result->captured = GetTickCount64();
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return result;
    OwnedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{.dwSize = sizeof(entry)};
    if (!Process32FirstW(snapshot.get(), &entry)) {
        result->valid = GetLastError() == ERROR_NO_MORE_FILES;
        return result;
    }
    do {
        if (stop.stop_requested()) return result;
        DWORD candidate_session = 0;
        OwnedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                        FALSE, entry.th32ProcessID));
        // Acquire the handle before checking the session. A PID from the
        // enumeration can be recycled between listing and OpenProcess.
        if (!process || !ProcessIdToSessionId(GetProcessId(process.get()), &candidate_session)
            || candidate_session != session) continue;
        std::wstring path(32768, L'\0');
        DWORD size = static_cast<DWORD>(path.size());
        if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &size)) continue;
        path.resize(size);
        path = normalize_hotkey_program_path(path);
        if (std::ranges::find(paths, path) != paths.end()) {
            auto item = std::make_unique<HotkeyProcess>(std::move(path), process.get());
            process.release();
            result->processes.push_back(std::move(item));
        }
    } while (Process32NextW(snapshot.get(), &entry));
    result->valid = GetLastError() == ERROR_NO_MORE_FILES;
    return result;
}

void HotkeyProcessMonitor::watch(const std::vector<std::wstring>& paths) {
    {
        std::lock_guard lock(mutex_);
        for (const auto& path : paths) {
            if (std::ranges::find(paths_, path) == paths_.end()) paths_.push_back(path);
        }
        ++revision_;
    }
    changed_.notify_all();
    if (worker_.joinable()) return;
    worker_ = std::jthread([this](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::vector<std::wstring> paths;
            std::size_t revision;
            {
                std::lock_guard lock(mutex_);
                paths = paths_;
                revision = revision_;
            }
            std::unique_ptr<HotkeyProcessSnapshot> snapshot;
            try { snapshot = enumerate(paths, stop); }
            catch (...) { /* Old snapshots expire and conditions fail closed. */ }
            std::unique_lock lock(mutex_);
            if (revision == revision_) pending_ = std::move(snapshot);
            changed_.wait_for(lock, stop, std::chrono::milliseconds(500),
                              [&] { return revision != revision_; });
        }
    });
}

std::unique_ptr<HotkeyProcessSnapshot> HotkeyProcessMonitor::take_snapshot() {
    std::unique_lock lock(mutex_, std::try_to_lock);
    return lock.owns_lock() ? std::move(pending_) : nullptr;
}

void HotkeyProcessMonitor::stop() noexcept {
    if (worker_.joinable()) {
        worker_.request_stop();
        changed_.notify_all();
        worker_.join();
    }
    pending_.reset();
    paths_.clear();
}

} // namespace simpilot
