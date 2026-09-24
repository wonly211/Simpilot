#pragma once

#include "menu_model.hpp"
#include "simpilot/program_search.hpp"
#include "simpilot/variable_expander.hpp"

#include <filesystem>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace simpilot {

class ProgramResolutionCache;

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
