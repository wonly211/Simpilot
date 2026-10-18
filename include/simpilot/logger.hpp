#pragma once

#include <filesystem>
#include <mutex>
#include <string_view>

namespace simpilot {

class Logger final {
public:
    explicit Logger(std::filesystem::path path);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void write(std::wstring_view message) noexcept;

private:
    void remove_expired_entries() noexcept;

    std::filesystem::path path_;
    std::mutex mutex_;
};

} // namespace simpilot
