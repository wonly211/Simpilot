#include "simpilot/logger.hpp"
#include "simpilot/text_encoding.hpp"

#include <Windows.h>

#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <format>
#include <string>
#include <string_view>

namespace simpilot {
namespace {

std::string timestamp() {
    SYSTEMTIME value{};
    GetLocalTime(&value);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}",
                       value.wYear, value.wMonth, value.wDay, value.wHour,
                       value.wMinute, value.wSecond, value.wMilliseconds);
}

std::string expiration_cutoff() {
    const auto cutoff = std::chrono::system_clock::now() - std::chrono::days(90);
    const auto value = std::chrono::system_clock::to_time_t(cutoff);
    std::tm local{};
    localtime_s(&local, &value);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}",
                       local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                       local.tm_hour, local.tm_min, local.tm_sec, 0);
}

bool has_timestamp_prefix(const std::string_view line) noexcept {
    constexpr std::size_t timestamp_length = 23;
    if (line.size() < timestamp_length) return false;
    for (std::size_t index = 0; index < timestamp_length; ++index) {
        const auto separator = [&]() -> char {
            switch (index) {
            case 4: case 7: return '-';
            case 10: return 'T';
            case 13: case 16: return ':';
            case 19: return '.';
            default: return '\0';
            }
        }();
        if (separator != '\0') {
            if (line[index] != separator) return false;
        } else if (!std::isdigit(static_cast<unsigned char>(line[index]))) {
            return false;
        }
    }
    return true;
}

} // namespace

Logger::Logger(std::filesystem::path path) : path_(std::move(path)) {
    remove_expired_entries();
}

void Logger::write(const std::wstring_view message) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        std::filesystem::create_directories(path_.parent_path());
        std::ofstream stream(path_, std::ios::binary | std::ios::app);
        if (!stream) return;
        stream << timestamp() << ' ' << encode_utf8(message) << "\r\n";
    } catch (...) {
        // Diagnostics must never prevent Simpilot from starting or opening a menu.
    }
}

void Logger::remove_expired_entries() noexcept {
    try {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path_, error)) return;

        std::ifstream source(path_, std::ios::binary);
        if (!source) return;
        const auto temporary = std::filesystem::path(path_.wstring() + L".tmp");
        std::ofstream destination(temporary, std::ios::binary | std::ios::trunc);
        if (!destination) return;

        const auto cutoff = expiration_cutoff();
        auto removed = false;
        std::string line;
        while (std::getline(source, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!has_timestamp_prefix(line) || line.substr(0, 23) < cutoff) {
                removed = true;
                continue;
            }
            destination << line << "\r\n";
        }
        source.close();
        destination.close();

        if (!removed) {
            std::filesystem::remove(temporary, error);
            return;
        }
        if (!MoveFileExW(temporary.c_str(), path_.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary, error);
        }
    } catch (...) {
    }
}

} // namespace simpilot
