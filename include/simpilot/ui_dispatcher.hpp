#pragma once

#include "simpilot/contribution_registry.hpp"

#include <Windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace simpilot {

// Cancellation can be published by a worker; callbacks execute only on the UI
// thread. An owner cancels its scope before destroying anything callbacks use.
class DispatchScope final {
public:
    DispatchScope() : active_(std::make_shared<std::atomic_bool>(true)) {}
    ~DispatchScope() { cancel(); }
    DispatchScope(const DispatchScope&) = delete;
    DispatchScope& operator=(const DispatchScope&) = delete;
    void cancel() noexcept { active_->store(false, std::memory_order_release); }

private:
    friend class UiDispatcher;
    std::shared_ptr<std::atomic_bool> active_;
};

class UiDispatcher final {
public:
    explicit UiDispatcher(HINSTANCE instance);
    ~UiDispatcher();
    UiDispatcher(const UiDispatcher&) = delete;
    UiDispatcher& operator=(const UiDispatcher&) = delete;

    // post() is worker-safe; repeat() and registration destruction are UI-only.
    [[nodiscard]] bool post(const DispatchScope& scope, std::function<void()> callback);
    [[nodiscard]] Registration repeat(
        const DispatchScope& scope, UINT interval_ms, std::function<void()> callback);
    void set_error_sink(std::function<void()> sink) { error_sink_ = std::move(sink); }
    void close() noexcept;

private:
    struct Work {
        std::weak_ptr<std::atomic_bool> active;
        std::function<void()> callback;
    };
    struct State {
        HWND window = nullptr;
        std::mutex mutex;
        std::vector<Work> pending;
        std::unordered_map<UINT_PTR, std::shared_ptr<Work>> timers;
        UINT_PTR next_timer = 1;
    };
    static LRESULT CALLBACK window_procedure(HWND, UINT, WPARAM, LPARAM);
    void invoke(const Work& work) noexcept;
    void drain();
    std::shared_ptr<State> state_;
    std::function<void()> error_sink_;
};

} // namespace simpilot
