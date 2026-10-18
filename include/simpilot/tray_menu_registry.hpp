#pragma once

#include "simpilot/contribution_registry.hpp"

#include <Windows.h>

namespace simpilot {

struct TrayCommand {
    std::string label_key;
    std::string group;
    std::function<bool()> enabled;
    std::function<void(HWND)> invoke;
};

class TrayMenuRegistry final {
public:
    [[nodiscard]] Registration add(std::string id, int order, TrayCommand value) {
        return commands_.add(std::move(id), order, std::move(value));
    }
    template<class Visitor> void visit(Visitor&& visitor) const {
        commands_.visit(std::forward<Visitor>(visitor));
    }
    std::size_t size() const noexcept { return commands_.size(); }
    ContributionRegistry<std::function<void(HWND)>> primary_actions;
private:
    ContributionRegistry<TrayCommand> commands_;
};

} // namespace simpilot
