#include "simpilot/config_watcher.hpp"

#include <array>
#include <fstream>

namespace simpilot {
namespace {

std::uint64_t file_hash(
    const std::filesystem::path& path, bool& readable) noexcept {
    constexpr std::uint64_t fnv_offset_basis = 1469598103934665603ULL;
    constexpr std::uint64_t fnv_prime = 1099511628211ULL;
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return 0;
        std::uint64_t result = fnv_offset_basis;
        std::array<char, 4096> buffer{};
        while (stream) {
            stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = stream.gcount();
            for (std::streamsize index = 0; index < count; ++index) {
                result ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
                result *= fnv_prime;
            }
        }
        if (stream.bad()) return 0;
        readable = true;
        return result;
    } catch (...) {
        return 0;
    }
}

} // namespace

ConfigWatcher::ConfigWatcher(std::filesystem::path directory,
                             std::vector<std::wstring> file_names,
                             ChangeCallback callback,
                             DiagnosticSink diagnostic_sink,
                             const std::chrono::milliseconds debounce)
    : directory_(std::filesystem::absolute(std::move(directory))),
      file_names_(std::move(file_names)), callback_(std::move(callback)),
      diagnostic_sink_(std::move(diagnostic_sink)), debounce_(debounce) {}

ConfigWatcher::~ConfigWatcher() {
    stop();
}

bool ConfigWatcher::start() noexcept {
    try {
        if (thread_.joinable()) return true;
        std::filesystem::create_directories(directory_);
        stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop_event_) {
            log(L"config watcher failed to create stop event");
            return false;
        }
        thread_ = std::thread([this] { watch_loop(); });
        return true;
    } catch (...) {
        if (stop_event_) CloseHandle(stop_event_);
        stop_event_ = nullptr;
        log(L"config watcher failed to start");
        return false;
    }
}

void ConfigWatcher::stop() noexcept {
    try {
        if (stop_event_) SetEvent(stop_event_);
        if (thread_.joinable()) thread_.join();
        if (stop_event_) CloseHandle(stop_event_);
        stop_event_ = nullptr;
    } catch (...) {
    }
}

std::vector<ConfigWatcher::FileState> ConfigWatcher::snapshot() const noexcept {
    std::vector<FileState> result;
    result.reserve(file_names_.size());
    for (const auto& file_name : file_names_) {
        FileState state;
        std::error_code error;
        const auto path = directory_ / file_name;
        state.exists = std::filesystem::is_regular_file(path, error);
        if (state.exists) {
            error.clear();
            state.size = std::filesystem::file_size(path, error);
            if (error) state.size = 0;
            error.clear();
            state.last_write = std::filesystem::last_write_time(path, error);
            if (error) state.last_write = {};
            state.content_hash = file_hash(path, state.readable);
        }
        result.push_back(state);
    }
    return result;
}

void ConfigWatcher::report_changes(std::vector<FileState>& previous) noexcept {
    const auto current = snapshot();
    if (current == previous) return;
    previous = current;
    try {
        if (callback_) callback_();
    } catch (...) {
        log(L"config watcher callback failed");
    }
}

void ConfigWatcher::watch_loop() noexcept {
    auto previous = snapshot();
    while (WaitForSingleObject(stop_event_, 0) != WAIT_OBJECT_0) {
        const auto notification = FindFirstChangeNotificationW(
            directory_.c_str(), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
                | FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_SIZE);
        if (notification == INVALID_HANDLE_VALUE) {
            log(L"config watcher could not observe configuration directory; retrying");
            if (WaitForSingleObject(stop_event_, 1000) == WAIT_OBJECT_0) return;
            continue;
        }
        log(L"config watcher ready");
        // A file can change after the first snapshot but before the directory
        // notification is armed. Re-snapshot immediately after arming it so
        // that this startup/retry window cannot lose a configuration update.
        report_changes(previous);
        const std::array<HANDLE, 2> handles{stop_event_, notification};
        while (true) {
            const auto result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                       handles.data(), FALSE, INFINITE);
            if (result == WAIT_OBJECT_0) {
                FindCloseChangeNotification(notification);
                return;
            }
            if (result != WAIT_OBJECT_0 + 1 || !FindNextChangeNotification(notification)) {
                log(L"config watcher notification failed; retrying");
                break;
            }
            if (WaitForSingleObject(stop_event_, static_cast<DWORD>(debounce_.count()))
                == WAIT_OBJECT_0) {
                FindCloseChangeNotification(notification);
                return;
            }
            report_changes(previous);
        }
        FindCloseChangeNotification(notification);
    }
}

void ConfigWatcher::log(const std::wstring_view message) const noexcept {
    if (!diagnostic_sink_) return;
    try {
        diagnostic_sink_(message);
    } catch (...) {
    }
}

} // namespace simpilot
