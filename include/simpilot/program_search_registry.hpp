#pragma once

#include "simpilot/contribution_registry.hpp"
#include "simpilot/program_search.hpp"

namespace simpilot {

class ProgramSearchRegistry final : public IProgramSearch {
public:
    [[nodiscard]] Registration add(
        std::string id, const IProgramSearch& provider, int order = 0) {
        return providers_.add(std::move(id), order, &provider);
    }
    [[nodiscard]] Registration on_changed(std::string id, std::function<void()> callback) {
        return listeners_.add(std::move(id), 0, std::move(callback));
    }
    void notify_changed() {
        listeners_.visit([](const auto&, const auto& callback) { callback(); });
    }
    [[nodiscard]] bool available() const override {
        bool result = false;
        providers_.visit([&](const std::string&, const IProgramSearch* provider) {
            result = result || provider->available();
        });
        return result;
    }
    [[nodiscard]] std::vector<ProgramCandidate> find_exact_file_name(
        const std::wstring& name) const override {
        std::vector<ProgramCandidate> result;
        providers_.visit([&](const std::string&, const IProgramSearch* provider) {
            if (!provider->available()) return;
            for (auto& candidate : provider->find_exact_file_name(name)) {
                if (std::ranges::none_of(result, [&](const ProgramCandidate& existing) {
                        return existing.path == candidate.path;
                    })) result.push_back(std::move(candidate));
            }
        });
        return result;
    }

private:
    ContributionRegistry<const IProgramSearch*> providers_;
    ContributionRegistry<std::function<void()>> listeners_;
};

} // namespace simpilot
