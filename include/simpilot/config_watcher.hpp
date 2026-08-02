#pragma once

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace simpilot {

class ConfigWatcher final {
public:
    using ChangeCallback = std::function<void()>;
    using DiagnosticSink = std::function<void(std::wstring_view)>;

    ConfigWatcher(std::filesystem::path directory,
                  std::vector<std::wstring> file_names,
                  ChangeCallback callback,
                  DiagnosticSink diagnostic_sink = {},
                  std::chrono::milliseconds debounce = std::chrono::milliseconds(450));
    ~ConfigWatcher();

    ConfigWatcher(const ConfigWatcher&) = delete;
    ConfigWatcher& operator=(const ConfigWatcher&) = delete;

    [[nodiscard]] bool start() noexcept;
    void stop() noexcept;

private:
    struct FileState {
        bool exists = false;
        std::uintmax_t size = 0;
        std::filesystem::file_time_type last_write{};

        bool operator==(const FileState&) const = default;
    };

    [[nodiscard]] std::vector<FileState> snapshot() const noexcept;
    void watch_loop() noexcept;
    void log(std::wstring_view message) const noexcept;

    std::filesystem::path directory_;
    std::vector<std::wstring> file_names_;
    ChangeCallback callback_;
    DiagnosticSink diagnostic_sink_;
    std::chrono::milliseconds debounce_;
    HANDLE stop_event_ = nullptr;
    std::thread thread_;
};

} // namespace simpilot
