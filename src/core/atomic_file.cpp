#include "simpilot/atomic_file.hpp"

#include <Windows.h>

#include <atomic>
#include <format>
#include <stdexcept>
#include <system_error>

namespace simpilot {
namespace {

std::atomic_uint64_t next_temporary_file_identifier{1};

std::filesystem::path reserve_temporary_path(
    const std::filesystem::path& target_path) {
    auto directory = target_path.parent_path();
    if (directory.empty()) directory = std::filesystem::current_path();
    std::filesystem::create_directories(directory);

    for (unsigned int attempt = 0; attempt < 1024; ++attempt) {
        const auto identifier = next_temporary_file_identifier.fetch_add(
            1, std::memory_order_relaxed);
        const auto file_name = std::format(
            L"{}.tmp.{}.{}.{}", target_path.filename().wstring(),
            GetCurrentProcessId(), GetCurrentThreadId(), identifier);
        const auto candidate = directory / file_name;
        const auto handle = CreateFileW(
            candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            return candidate;
        }
        const auto error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            throw std::system_error(
                static_cast<int>(error), std::system_category(),
                "Unable to reserve an atomic-write temporary file");
        }
    }
    throw std::runtime_error("Unable to reserve a unique atomic-write temporary file");
}

} // namespace

AtomicFileReplacement::AtomicFileReplacement(std::filesystem::path target_path)
    : target_path_(std::filesystem::absolute(std::move(target_path))),
      temporary_path_(reserve_temporary_path(target_path_)) {}

AtomicFileReplacement::~AtomicFileReplacement() {
    if (committed_ || temporary_path_.empty()) return;
    std::error_code error;
    std::filesystem::remove(temporary_path_, error);
}

const std::filesystem::path& AtomicFileReplacement::temporary_path() const noexcept {
    return temporary_path_;
}

bool AtomicFileReplacement::commit() noexcept {
    if (committed_) return true;
    if (!MoveFileExW(temporary_path_.c_str(), target_path_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return false;
    }
    committed_ = true;
    return true;
}

} // namespace simpilot
