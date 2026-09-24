#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace simpilot {

class Registration final {
public:
    Registration() = default;
    explicit Registration(std::function<void()> release)
        : release_(std::move(release)) {}
    ~Registration() { reset(); }
    Registration(const Registration&) = delete;
    Registration& operator=(const Registration&) = delete;
    Registration(Registration&& other) noexcept
        : release_(std::exchange(other.release_, {})) {}
    Registration& operator=(Registration&& other) noexcept {
        if (this != &other) {
            reset();
            release_ = std::exchange(other.release_, {});
        }
        return *this;
    }
    void reset() noexcept {
        if (auto release = std::exchange(release_, {})) release();
    }

private:
    std::function<void()> release_;
};

// UI-thread-only storage. Snapshots keep an executing callback alive while
// revocation prevents later callbacks in that same traversal from executing.
template <typename Contribution>
class ContributionRegistry final {
    struct Entry {
        std::string id;
        int order;
        Contribution value;
        bool active = true;
    };
    struct State {
        std::vector<std::shared_ptr<Entry>> entries;
    };

public:
    ContributionRegistry() : state_(std::make_shared<State>()) {}
    ContributionRegistry(const ContributionRegistry&) = delete;
    ContributionRegistry& operator=(const ContributionRegistry&) = delete;

    [[nodiscard]] Registration add(std::string id, int order, Contribution value) {
        if (id.empty()) throw std::invalid_argument("Empty contribution identifier");
        if (std::ranges::any_of(state_->entries, [&id](const auto& entry) {
                return entry->id == id;
            })) {
            throw std::invalid_argument("Duplicate contribution identifier: " + id);
        }
        auto entry = std::make_shared<Entry>(Entry{
            std::move(id), order, std::move(value), true});
        Registration registration([weak = std::weak_ptr<State>(state_), entry] {
            entry->active = false;
            if (auto state = weak.lock()) {
                std::erase(state->entries, entry);
            }
        });
        const auto position = std::upper_bound(
            state_->entries.begin(), state_->entries.end(), order,
            [](int requested, const auto& existing) { return requested < existing->order; });
        state_->entries.insert(position, entry);
        return registration;
    }

    template <typename Visitor>
    void visit(Visitor&& visitor) const {
        visit_guarded([&visitor](const auto& id, const auto& value, auto) {
            visitor(id, value);
        });
    }

    template <typename Visitor>
    void visit_guarded(Visitor&& visitor) const {
        const auto snapshot = state_->entries;
        for (const auto& entry : snapshot) {
            if (entry->active) {
                visitor(entry->id, entry->value, [weak = std::weak_ptr<Entry>(entry)] {
                    const auto current = weak.lock();
                    return current && current->active;
                });
            }
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return state_->entries.size(); }

private:
    std::shared_ptr<State> state_;
};

} // namespace simpilot
