#pragma once

#include "simpilot/hotkey.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace simpilot {

enum class WindowsHotKeyDecision {
    pass,
    block,
    block_and_release_windows,
    pass_and_restore_windows,
};

struct WindowsHotKeyTransition {
    WindowsHotKeyDecision decision = WindowsHotKeyDecision::pass;
    bool left_windows = false;
    bool right_windows = false;
};

class WindowsHotKeyState final {
public:
    [[nodiscard]] WindowsHotKeyTransition handle(
        UINT virtual_key, bool key_down, bool key_up,
        std::uint32_t blocked_mask, bool exact_windows_modifier = true) noexcept;
    [[nodiscard]] WindowsHotKeyTransition restore_suppressed_windows() noexcept;
    void cancel_suppression(bool left_windows, bool right_windows) noexcept;

private:
    bool left_windows_down_ = false;
    bool right_windows_down_ = false;
    bool left_windows_suppressed_ = false;
    bool right_windows_suppressed_ = false;
    std::array<bool, 26> physical_keys_down_{};
    std::array<bool, 26> blocked_keys_{};
};

enum class KeyboardCaptureResultKind {
    captured,
    cancelled,
};

struct KeyboardCaptureResult {
    KeyboardCaptureResultKind kind = KeyboardCaptureResultKind::cancelled;
    HotKeyGesture gesture;
};

class KeyboardManager final {
public:
    using State = std::array<bool, 26>;
    using CaptureHandler = std::function<void(const KeyboardCaptureResult&)>;

    KeyboardManager();
    ~KeyboardManager();

    KeyboardManager(const KeyboardManager&) = delete;
    KeyboardManager& operator=(const KeyboardManager&) = delete;

    [[nodiscard]] bool start(HWND target_window, const State& state) noexcept;
    void update(const State& state) noexcept;
    [[nodiscard]] bool register_binding(int identifier, const HotKeyBinding& binding);
    [[nodiscard]] bool register_standard(int identifier, const HotKeyGesture& gesture);
    void unregister_all() noexcept;
    [[nodiscard]] bool probe_available(const HotKeyGesture& gesture) const noexcept;

    [[nodiscard]] bool begin_capture(CaptureHandler handler) noexcept;
    void end_capture() noexcept;

    [[nodiscard]] DWORD last_error() const noexcept;

private:
    struct ForcedRegistration {
        int identifier = 0;
        HotKeyGesture gesture;
        bool key_is_down = false;
    };

    struct RegistrationRequest {
        int identifier = 0;
        HotKeyGesture gesture;
        bool accepted = false;
    };

    [[nodiscard]] bool create_capture_window() noexcept;
    [[nodiscard]] bool start_hook_thread() noexcept;
    [[nodiscard]] bool send_control(
        UINT message, WPARAM wparam = 0, LPARAM lparam = 0,
        DWORD_PTR* result = nullptr) noexcept;
    void stop() noexcept;

    static DWORD WINAPI hook_thread_entry(void* context) noexcept;
    DWORD hook_thread_main() noexcept;
    static LRESULT CALLBACK hook_window_procedure(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_hook_window_message(
        UINT message, WPARAM wparam, LPARAM lparam) noexcept;

    static LRESULT CALLBACK capture_window_procedure(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_capture_window_message(
        UINT message, WPARAM wparam, LPARAM lparam) noexcept;

    static LRESULT CALLBACK keyboard_hook(int code, WPARAM wparam, LPARAM lparam);
    [[nodiscard]] LRESULT handle_capture_event(
        WPARAM message, KBDLLHOOKSTRUCT& event) noexcept;
    [[nodiscard]] LRESULT handle_registered_hotkey(
        WPARAM message, const KBDLLHOOKSTRUCT& event) noexcept;
    [[nodiscard]] LRESULT handle_key(
        WPARAM message, const KBDLLHOOKSTRUCT& event) noexcept;
    [[nodiscard]] static bool send_windows_transition(
        const WindowsHotKeyTransition& transition) noexcept;

    [[nodiscard]] static std::uint32_t mask_for_state(const State& state) noexcept;

    static KeyboardManager* owner_;

    // UI-thread state.
    HWND target_window_ = nullptr;
    HWND capture_window_ = nullptr;
    HANDLE hook_thread_ = nullptr;
    HANDLE hook_ready_event_ = nullptr;
    DWORD hook_thread_id_ = 0;
    DWORD last_error_ = ERROR_SUCCESS;
    UINT capture_session_ = 0;
    bool capture_active_ = false;
    CaptureHandler capture_handler_;
    std::vector<int> standard_identifiers_;
    std::vector<HotKeyGesture> used_gestures_;

    // Keyboard-thread state. It is mutated only by the hook thread or by
    // synchronous control messages executed on that thread.
    HWND hook_window_ = nullptr;
    HHOOK hook_ = nullptr;
    std::uint32_t blocked_mask_ = 0;
    WindowsHotKeyState windows_hotkey_state_;
    std::array<bool, 256> pressed_keys_{};
    std::vector<ForcedRegistration> forced_registrations_;
    std::uint32_t windows_override_mask_ = 0;
    std::array<int, 26> windows_override_identifiers_{};
    std::array<bool, 26> windows_override_keys_down_{};
    UINT hook_capture_session_ = 0;

    class RecordingState;
    std::unique_ptr<RecordingState> recording_state_;
};

} // namespace simpilot
