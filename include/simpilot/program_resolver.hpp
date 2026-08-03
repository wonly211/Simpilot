#pragma once

#include "simpilot/menu_model.hpp"
#include "simpilot/variable_expander.hpp"

#include <filesystem>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace simpilot {

class ProgramResolutionCache;

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

class ProgramResolver final {
public:
    explicit ProgramResolver(const IProgramSearch* search = nullptr,
                             ProgramResolutionCache* cache = nullptr,
                             ProgramCandidateSelector candidate_selector = {});
    [[nodiscard]] std::optional<std::filesystem::path> resolve(const std::wstring& executable) const;
    [[nodiscard]] std::vector<ProgramCandidate> find_candidates(
        const std::wstring& executable) const;

private:
    const IProgramSearch* search_;
    ProgramResolutionCache* cache_;
    ProgramCandidateSelector candidate_selector_;
};

class MenuResolutionService final {
public:
    explicit MenuResolutionService(ProgramResolver resolver = ProgramResolver(nullptr));
    void resolve(MenuDocument& document, const VariableExpander& variable_expander) const;

private:
    ProgramResolver resolver_;
};

} // namespace simpilot
