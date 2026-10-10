#pragma once
#include "simpilot/hotkey.hpp"
#include <array>
#include <vector>

namespace simpilot {
struct ConditionalHotkeyRegistration {
    int identifier = 0;
    HotKeyGesture gesture;
    HotkeyActivationConditions activation;
};
struct ConditionalHotkeyDecision {
    int invoke = 0;
    bool consume = false;
};

// The first down chooses ownership, including the decision to pass. Changing
// conditions or modifiers while held must never turn a repeat into a new press.
class ConditionalHotkeyState final {
public:
    std::vector<ConditionalHotkeyRegistration> registrations;
    template<class ProcessMatches>
    ConditionalHotkeyDecision handle(UINT key, bool down, UINT modifiers,
        std::optional<unsigned> locks, ProcessMatches&& process_matches) noexcept {
        if (key >= presses_.size()) return {};
        auto& press = presses_[key];
        if (!down) {
            const auto consume = press.consume;
            press = {};
            return {0, consume};
        }
        if (press.down) return {0, press.consume};
        press.down = true;
        for (const auto& registration : registrations) {
            if (registration.gesture.virtual_key != key || registration.gesture.modifiers != modifiers) continue;
            const auto mask = registration.activation.allowed_lock_states();
            if (mask != 0xff && (!locks || *locks > 7 || !(mask & (1U << *locks)))) continue;
            if (registration.activation.require_running_program && !process_matches(registration)) continue;
            press.consume = true;
            return {registration.identifier, true};
        }
        return {};
    }
    void clear_registrations() noexcept { registrations.clear(); }
    void captured(UINT key, bool down) noexcept {
        if (key >= presses_.size()) return;
        if (!down) presses_[key] = {};
        else presses_[key].down = true;
    }
    void reset() noexcept { registrations.clear(); presses_ = {}; }
private:
    struct Press { bool down = false; bool consume = false; };
    std::array<Press, 256> presses_{};
};
} // namespace simpilot
