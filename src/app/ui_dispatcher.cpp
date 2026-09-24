#include "simpilot/ui_dispatcher.hpp"

#include <limits>
#include <stdexcept>

namespace simpilot {
namespace {
constexpr wchar_t dispatcher_class[] = L"Simpilot.UiDispatcher";
constexpr UINT dispatch_message = WM_APP + 1;
}

UiDispatcher::UiDispatcher(HINSTANCE instance) : state_(std::make_shared<State>()) {
    const WNDCLASSW window_class{
        .lpfnWndProc = &UiDispatcher::window_procedure,
        .hInstance = instance,
        .lpszClassName = dispatcher_class,
    };
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("Cannot register UI dispatcher window");
    }
    state_->window = CreateWindowExW(0, dispatcher_class, L"", 0, 0, 0, 0, 0,
                                     HWND_MESSAGE, nullptr, instance, this);
    if (!state_->window) throw std::runtime_error("Cannot create UI dispatcher window");
}

UiDispatcher::~UiDispatcher() {
    close();
}

void UiDispatcher::close() noexcept {
    HWND window = nullptr;
    {
        std::lock_guard lock(state_->mutex);
        window = std::exchange(state_->window, nullptr);
        state_->pending.clear();
    }
    for (const auto& [id, work] : state_->timers) {
        (void)work;
        KillTimer(window, id);
    }
    state_->timers.clear();
    if (window) DestroyWindow(window);
}

bool UiDispatcher::post(const DispatchScope& scope, std::function<void()> callback) {
    if (!callback || !scope.active_->load(std::memory_order_acquire)) return false;
    std::lock_guard lock(state_->mutex);
    if (!state_->window) return false;
    state_->pending.push_back({scope.active_, std::move(callback)});
    if (!PostMessageW(state_->window, dispatch_message, 0, 0)) {
        state_->pending.pop_back();
        return false;
    }
    return true;
}

Registration UiDispatcher::repeat(const DispatchScope& scope, UINT interval_ms,
                                  std::function<void()> callback) {
    if (!state_->window || !callback || !scope.active_->load(std::memory_order_acquire)
        || state_->next_timer == std::numeric_limits<UINT_PTR>::max()) {
        throw std::invalid_argument("Invalid UI timer");
    }
    const auto id = state_->next_timer++;
    auto work = std::make_shared<Work>(Work{scope.active_, std::move(callback)});
    Registration registration([weak = std::weak_ptr<State>(state_), id] {
        if (auto state = weak.lock()) {
            if (state->window) KillTimer(state->window, id);
            state->timers.erase(id);
        }
    });
    state_->timers.emplace(id, std::move(work));
    if (!SetTimer(state_->window, id, interval_ms, nullptr)) {
        throw std::runtime_error("Cannot start UI timer");
    }
    return registration;
}

void UiDispatcher::invoke(const Work& work) noexcept {
    const auto active = work.active.lock();
    if (!active || !active->load(std::memory_order_acquire)) return;
    try {
        work.callback();
    } catch (...) {
        try { if (error_sink_) error_sink_(); } catch (...) {}
    }
}

void UiDispatcher::drain() {
    std::vector<Work> work;
    {
        std::lock_guard lock(state_->mutex);
        work.swap(state_->pending);
    }
    for (const auto& item : work) invoke(item);
}

LRESULT CALLBACK UiDispatcher::window_procedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto* dispatcher = reinterpret_cast<UiDispatcher*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (dispatcher) {
        if (message == dispatch_message) {
            dispatcher->drain();
            return 0;
        }
        if (message == WM_TIMER) {
            const auto found = dispatcher->state_->timers.find(wparam);
            if (found != dispatcher->state_->timers.end()) {
                const auto work = found->second;
                dispatcher->invoke(*work);
            }
            return 0;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace simpilot
