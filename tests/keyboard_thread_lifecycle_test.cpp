#include "keyboard_manager.hpp"

#include <iostream>

namespace {
class TestStage final : public simpilot::IKeyboardProcessingStage {
public:
    std::atomic<DWORD> thread{0};
    std::atomic_int resets{0};
    std::atomic_bool exit_on_timer{false};
    simpilot::MappingEventResult handle(UINT, const KBDLLHOOKSTRUCT&) noexcept override { return {}; }
    void on_timer() noexcept override {
        thread = GetCurrentThreadId();
        if (exit_on_timer.exchange(false)) PostQuitMessage(0);
    }
    void set_foreground_process(std::wstring) noexcept override { thread = GetCurrentThreadId(); }
    void reset(bool) noexcept override { thread = GetCurrentThreadId(); ++resets; }
    bool consume_diagnostic() noexcept override { return false; }
};
}

int wmain() {
    for (int iteration = 0; iteration < 32; ++iteration) {
        simpilot::KeyboardManager::State state{};
        state[static_cast<std::size_t>(L'G' - L'A')] = true;

        simpilot::KeyboardManager keyboard_manager;
        if (!keyboard_manager.start(nullptr, state)) {
            std::wcerr << L"Dedicated keyboard thread startup failed with Windows error "
                       << keyboard_manager.last_error() << L" on iteration "
                       << iteration << L".\n";
            return 1;
        }

        state[static_cast<std::size_t>(L'G' - L'A')] = false;
        state[static_cast<std::size_t>(L'F' - L'A')] = true;
        if (!keyboard_manager.update(state)) return 1;
        const auto stage = std::make_shared<TestStage>();
        if (!keyboard_manager.set_processing_stage(stage)
            || stage->thread == 0 || stage->thread == GetCurrentThreadId()) {
            std::wcerr << L"Processing stage was not installed on the dedicated hook thread.\n";
            return 1;
        }
        const auto thread = stage->thread.load();
        if (!keyboard_manager.start(nullptr) || stage->thread != thread) {
            std::wcerr << L"Starting an active keyboard manager created a second hook thread.\n";
            return 1;
        }
        if (!keyboard_manager.begin_capture([](const auto&) {})) return 1;
        keyboard_manager.end_capture();
        if (stage->resets == 0) {
            std::wcerr << L"Capture must reset pending stage state before recording.\n";
            return 1;
        }
        if (iteration == 0) {
            bool cancelled = false;
            if (!keyboard_manager.begin_mapping_capture(simpilot::CaptureMode::mapping_trigger,
                    [&](const auto& result) {
                        cancelled = result.kind == simpilot::KeyboardMappingCaptureResultKind::cancelled;
                    })) return 1;
            stage->exit_on_timer = true;
            for (int wait = 0; wait < 200 && keyboard_manager.running(); ++wait) Sleep(10);
            if (keyboard_manager.running() || !keyboard_manager.start(nullptr) || !cancelled) {
                std::wcerr << L"Thread recovery did not invalidate the pending mapping capture.\n";
                return 1;
            }
            if (stage->thread == 0 || stage->thread == GetCurrentThreadId()) {
                std::wcerr << L"Processing stage was not restored on the replacement hook thread.\n";
                return 1;
            }
        }
        if (!keyboard_manager.set_processing_stage({}) || stage.use_count() != 1) {
            std::wcerr << L"Detaching the stage did not release its hook-thread ownership.\n";
            return 1;
        }
        const simpilot::HotKeyGesture owned_gesture{
            MOD_CONTROL | MOD_SHIFT | MOD_ALT, VK_F24};
        if (!keyboard_manager.register_standard(9001, owned_gesture)) {
            std::wcerr << L"Could not register the lifecycle test hotkey.\n";
            return 1;
        }
        if (!keyboard_manager.probe_available(owned_gesture)) {
            std::wcerr << L"A Simpilot-owned hotkey was reported as an external conflict.\n";
            return 1;
        }
        keyboard_manager.unregister_all();
    }

    std::wcout << L"Repeated keyboard thread lifecycle passed without a dangling hook thread.\n";
    return 0;
}
