#pragma once

#include <filesystem>

namespace simpilot {

class AtomicFileReplacement final {
public:
    explicit AtomicFileReplacement(std::filesystem::path target_path);
    ~AtomicFileReplacement();

    AtomicFileReplacement(const AtomicFileReplacement&) = delete;
    AtomicFileReplacement& operator=(const AtomicFileReplacement&) = delete;

    [[nodiscard]] const std::filesystem::path& temporary_path() const noexcept;
    [[nodiscard]] bool commit() noexcept;

private:
    std::filesystem::path target_path_;
    std::filesystem::path temporary_path_;
    bool committed_ = false;
};

} // namespace simpilot
