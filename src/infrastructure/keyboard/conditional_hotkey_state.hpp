#pragma once
#include "simpilot/hotkey.hpp"
#include <array>
#include <memory>
#include <vector>

namespace simpilot {
struct HotkeyProcess;
struct ConditionalHotkeyRegistration {
    int identifier = 0;
    HotKeyGesture gesture;
    HotkeyActivationConditions activation;
    // Retain the exact process instance selected at first down, across snapshot updates.
    std::shared_ptr<const HotkeyProcess> pending_process;
    // Bounded FIFO: separate complete presses may queue before UI dispatch.
    // No allocation in the low-level hook; overload fails closed.
    std::array<std::shared_ptr<const HotkeyProcess>, 32> queued_processes{};
    std::size_t queue_head = 0;
    std::size_t queue_size = 0;
    bool queue_process() noexcept {
        if (queue_size == queued_processes.size()) return false;
        queued_processes[(queue_head + queue_size++) % queued_processes.size()] = std::move(pending_process);
        return true;
    }
    void cancel_queued_process() noexcept {
        if (queue_size) queued_processes[(queue_head + --queue_size) % queued_processes.size()].reset();
    }
    std::shared_ptr<const HotkeyProcess> take_queued_process() noexcept {
        if (!queue_size) return {};
        auto result = std::move(queued_processes[queue_head]);
        queue_head = (queue_head + 1) % queued_processes.size();
        --queue_size;
        return result;
    }
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
        const ConditionalHotkeyRegistration* selected = nullptr;
        for (auto& registration : registrations) {
            if (registration.gesture.virtual_key != key || registration.gesture.modifiers != modifiers) continue;
            const auto mask = registration.activation.allowed_lock_states();
            if (mask != 0xff && (!locks || *locks > 7 || !(mask & (1U << *locks)))) continue;
            if ((registration.activation.require_running_program || registration.activation.require_foreground_program)
                && !process_matches(registration)) continue;
            if (!selected || registration.activation.priority.value_or(0) > selected->activation.priority.value_or(0))
                selected = &registration;
        }
        if (selected) {
            press.consume = true;
            return {selected->identifier, true};
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
