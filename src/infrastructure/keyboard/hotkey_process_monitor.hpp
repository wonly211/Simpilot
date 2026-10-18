#pragma once

#include "simpilot/hotkey_activation.hpp"
#include <Windows.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace simpilot {

struct HotkeyProcess {
    std::wstring path;
    HANDLE handle = nullptr;
    DWORD identifier = 0;
    HotkeyProcess(std::wstring value, HANDLE process) : path(std::move(value)), handle(process), identifier(GetProcessId(process)) {}
    ~HotkeyProcess() { if (handle) CloseHandle(handle); }
    HotkeyProcess(const HotkeyProcess&) = delete;
    HotkeyProcess& operator=(const HotkeyProcess&) = delete;
};

struct HotkeyProcessSnapshot {
    ULONGLONG captured = 0;
    bool valid = false;
    std::vector<std::shared_ptr<HotkeyProcess>> processes;
    [[nodiscard]] std::shared_ptr<const HotkeyProcess> matching_process(
        const std::vector<std::wstring>& normalized_paths, DWORD foreground_identifier = 0,
        ULONGLONG now = GetTickCount64()) const noexcept;
    [[nodiscard]] bool matches(const std::vector<std::wstring>& normalized_paths,
                               ULONGLONG now = GetTickCount64()) const noexcept;
    [[nodiscard]] bool matches_foreground(const std::vector<std::wstring>& normalized_paths,
                                         HWND window = GetForegroundWindow(),
                                         ULONGLONG now = GetTickCount64()) const noexcept;
};

// Enumeration and handle acquisition run on a worker. Snapshots are handed
// off only from a hook-window message, never while processing a keyboard event.
class HotkeyProcessMonitor final {
public:
    ~HotkeyProcessMonitor() { stop(); }
    void watch(const std::vector<std::wstring>& paths);
    void stop() noexcept;
    [[nodiscard]] std::unique_ptr<HotkeyProcessSnapshot> take_snapshot();
    [[nodiscard]] static std::unique_ptr<HotkeyProcessSnapshot> enumerate(
        const std::vector<std::wstring>& paths, std::stop_token stop = {});
private:
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::vector<std::wstring> paths_;
    std::unique_ptr<HotkeyProcessSnapshot> pending_;
    std::size_t revision_ = 0;
    std::jthread worker_;
};

} // namespace simpilot
