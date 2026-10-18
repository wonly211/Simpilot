#pragma once
#include <filesystem>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace simpilot {
struct ProgramCandidate {
    std::filesystem::path path;
    std::uint64_t version = 0;
    std::filesystem::file_time_type last_write_time{};
};

using ProgramCandidateSelector = std::function<std::optional<std::filesystem::path>(
    const std::wstring&, const std::vector<ProgramCandidate>&)>;

class IProgramSearch {
public:
    virtual ~IProgramSearch() = default;
    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::vector<ProgramCandidate> find_exact_file_name(
        const std::wstring& file_name) const = 0;
};

}
