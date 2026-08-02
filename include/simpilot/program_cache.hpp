#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace simpilot {

class ProgramResolutionCache final {
public:
    explicit ProgramResolutionCache(std::filesystem::path path);

    ProgramResolutionCache(const ProgramResolutionCache&) = delete;
    ProgramResolutionCache& operator=(const ProgramResolutionCache&) = delete;

    [[nodiscard]] std::optional<std::filesystem::path> find(const std::wstring& executable);
    void store(const std::wstring& executable, const std::filesystem::path& resolved_path);
    [[nodiscard]] std::size_t size() const noexcept;

private:
    void load() noexcept;
    void save() noexcept;
    [[nodiscard]] static std::wstring key_for(std::wstring executable);

    std::filesystem::path path_;
    std::unordered_map<std::wstring, std::filesystem::path> entries_;
};

} // namespace simpilot
