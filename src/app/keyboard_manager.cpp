#include "keyboard_manager.hpp"

#include "KeyboardManagerState.h"
#include "LowlevelKeyboardEvent.h"
#include "Shortcut.h"

#include <algorithm>
#include <format>

namespace simpilot {
namespace {

constexpr ULONG_PTR simpilot_injected_event = 0x53494D50;
constexpr int probe_identifier = 0x6F00;
constexpr DWORD hook_start_timeout_ms = 5000;
constexpr DWORD hook_command_timeout_ms = 2000;

constexpr auto hook_window_class_name = L"Simpilot.KeyboardHookThread";
constexpr auto capture_window_class_name = L"Simpilot.KeyboardCaptureReceiver";

constexpr UINT update_state_message = WM_APP + 0x550;
constexpr UINT add_registration_message = WM_APP + 0x551;
constexpr UINT clear_registrations_message = WM_APP + 0x552;
constexpr UINT begin_capture_message = WM_APP + 0x553;
constexpr UINT end_capture_message = WM_APP + 0x554;
constexpr UINT shutdown_hook_message = WM_APP + 0x555;
constexpr UINT capture_result_message = WM_APP + 0x556;

UINT current_modifiers(const std::array<bool, 256>& keys) noexcept {
    UINT result = 0;
    if (keys[VK_CONTROL] || keys[VK_LCONTROL] || keys[VK_RCONTROL]) result |= MOD_CONTROL;
    if (keys[VK_MENU] || keys[VK_LMENU] || keys[VK_RMENU]) result |= MOD_ALT;
    if (keys[VK_SHIFT] || keys[VK_LSHIFT] || keys[VK_RSHIFT]) result |= MOD_SHIFT;
    if (keys[VK_LWIN] || keys[VK_RWIN]) result |= MOD_WIN;
    return result;
}

UINT shortcut_modifiers(const Shortcut& shortcut) noexcept {
    UINT modifiers = 0;
    if (shortcut.winKey != ModifierKey::Disabled) modifiers |= MOD_WIN;
    if (shortcut.ctrlKey != ModifierKey::Disabled) modifiers |= MOD_CONTROL;
    if (shortcut.altKey != ModifierKey::Disabled) modifiers |= MOD_ALT;
    if (shortcut.shiftKey != ModifierKey::Disabled) modifiers |= MOD_SHIFT;
    return modifiers;
}

LPARAM pack_capture_result(
    const KeyboardCaptureResultKind kind,
    const UINT modifiers, const UINT virtual_key) noexcept {
    const auto packed = static_cast<std::uint32_t>(virtual_key & 0xFFFFU)
        | static_cast<std::uint32_t>((modifiers & 0xFFU) << 16U)
        | (kind == KeyboardCaptureResultKind::cancelled ? 1U : 0U) << 24U;
    return static_cast<LPARAM>(packed);
}

KeyboardCaptureResult unpack_capture_result(const LPARAM value) noexcept {
    const auto packed = static_cast<std::uint32_t>(value);
    return {
        .kind = ((packed >> 24U) & 1U) != 0
            ? KeyboardCaptureResultKind::cancelled
            : KeyboardCaptureResultKind::captured,
        .gesture = {
            (packed >> 16U) & 0xFFU,
            packed & 0xFFFFU,
        },
    };
}

WindowsHotKeyTransition transition(
    const WindowsHotKeyDecision decision,
    const bool left_windows = false,
    const bool right_windows = false) noexcept {
    return {decision, left_windows, right_windows};
}

} // namespace

class KeyboardManager::RecordingState final {
public:
    KeyboardManagerState state;
};

KeyboardManager* KeyboardManager::owner_ = nullptr;

WindowsHotKeyTransition WindowsHotKeyState::handle(
    const UINT virtual_key, const bool key_down, const bool key_up,
    const std::uint32_t blocked_mask, const bool exact_windows_modifier) noexcept {
    if (virtual_key == VK_LWIN) {
        if (key_down) {
            left_windows_down_ = true;
            return transition(left_windows_suppressed_
                ? WindowsHotKeyDecision::block : WindowsHotKeyDecision::pass);
        }
        if (key_up) {
            left_windows_down_ = false;
            if (left_windows_suppressed_) {
                left_windows_suppressed_ = false;
                return transition(WindowsHotKeyDecision::block);
            }
        }
        return transition(WindowsHotKeyDecision::pass);
    }
    if (virtual_key == VK_RWIN) {
        if (key_down) {
            right_windows_down_ = true;
            return transition(right_windows_suppressed_
                ? WindowsHotKeyDecision::block : WindowsHotKeyDecision::pass);
        }
        if (key_up) {
            right_windows_down_ = false;
            if (right_windows_suppressed_) {
                right_windows_suppressed_ = false;
                return transition(WindowsHotKeyDecision::block);
            }
        }
        return transition(WindowsHotKeyDecision::pass);
    }
    if (virtual_key < L'A' || virtual_key > L'Z') {
        return key_down ? restore_suppressed_windows()
                        : transition(WindowsHotKeyDecision::pass);
    }

    const auto index = static_cast<std::size_t>(virtual_key - L'A');
    if (key_up) {
        physical_keys_down_[index] = false;
        if (blocked_keys_[index]) {
            blocked_keys_[index] = false;
            return transition(WindowsHotKeyDecision::block);
        }
        return transition(WindowsHotKeyDecision::pass);
    }
    if (!key_down) return transition(WindowsHotKeyDecision::pass);

    if (blocked_keys_[index]) return transition(WindowsHotKeyDecision::block);
    if (physical_keys_down_[index]) return restore_suppressed_windows();
    physical_keys_down_[index] = true;

    const auto selected = ((blocked_mask >> index) & 1U) != 0;
    if (!selected || !exact_windows_modifier
        || (!left_windows_down_ && !right_windows_down_)) {
        return restore_suppressed_windows();
    }
    blocked_keys_[index] = true;
    const auto release_left = left_windows_down_ && !left_windows_suppressed_;
    const auto release_right = right_windows_down_ && !right_windows_suppressed_;
    left_windows_suppressed_ = left_windows_suppressed_ || release_left;
    right_windows_suppressed_ = right_windows_suppressed_ || release_right;
    return transition(release_left || release_right
        ? WindowsHotKeyDecision::block_and_release_windows
        : WindowsHotKeyDecision::block, release_left, release_right);
}

WindowsHotKeyTransition WindowsHotKeyState::restore_suppressed_windows() noexcept {
    const auto restore_left = left_windows_suppressed_ && left_windows_down_;
    const auto restore_right = right_windows_suppressed_ && right_windows_down_;
    left_windows_suppressed_ = false;
    right_windows_suppressed_ = false;
    return transition(restore_left || restore_right
        ? WindowsHotKeyDecision::pass_and_restore_windows
        : WindowsHotKeyDecision::pass, restore_left, restore_right);
}

void WindowsHotKeyState::cancel_suppression(
    const bool left_windows, const bool right_windows) noexcept {
    if (left_windows) left_windows_suppressed_ = false;
    if (right_windows) right_windows_suppressed_ = false;
}

KeyboardManager::KeyboardManager()
    : recording_state_(std::make_unique<RecordingState>()) {}

KeyboardManager::~KeyboardManager() {
    unregister_all();
    stop();
}

std::uint32_t KeyboardManager::mask_for_state(const State& state) noexcept {
    std::uint32_t mask = 0;
    for (std::size_t index = 0; index < state.size(); ++index) {
        if (index != static_cast<std::size_t>(L'L' - L'A') && state[index]) {
            mask |= std::uint32_t{1} << index;
        }
    }
    return mask;
}

bool KeyboardManager::start(
    const HWND target_window, const State& state) noexcept {
    if (hook_thread_) {
        update(state);
        return true;
    }
    if (owner_ && owner_ != this) {
        last_error_ = ERROR_ALREADY_EXISTS;
        return false;
    }
    target_window_ = target_window;
    blocked_mask_ = mask_for_state(state);
    if (!create_capture_window() || !start_hook_thread()) {
        stop();
        return false;
    }
    return true;
}

bool KeyboardManager::create_capture_window() noexcept {
    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW window_class{
        .lpfnWndProc = &KeyboardManager::capture_window_procedure,
        .hInstance = instance,
        .lpszClassName = capture_window_class_name,
    };
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        last_error_ = GetLastError();
        return false;
    }
    capture_window_ = CreateWindowExW(
        0, capture_window_class_name, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    if (!capture_window_) {
        last_error_ = GetLastError();
        return false;
    }
    return true;
}

bool KeyboardManager::start_hook_thread() noexcept {
    hook_ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!hook_ready_event_) {
        last_error_ = GetLastError();
        return false;
    }
    hook_thread_ = CreateThread(
        nullptr, 0, &KeyboardManager::hook_thread_entry,
        this, 0, &hook_thread_id_);
    if (!hook_thread_) {
        last_error_ = GetLastError();
        return false;
    }
    const auto wait = WaitForSingleObject(hook_ready_event_, hook_start_timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        last_error_ = wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError();
        return false;
    }
    return hook_window_ && hook_;
}

DWORD WINAPI KeyboardManager::hook_thread_entry(void* context) noexcept {
    return static_cast<KeyboardManager*>(context)->hook_thread_main();
}

DWORD KeyboardManager::hook_thread_main() noexcept {
    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW window_class{
        .lpfnWndProc = &KeyboardManager::hook_window_procedure,
        .hInstance = instance,
        .lpszClassName = hook_window_class_name,
    };
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        last_error_ = GetLastError();
        SetEvent(hook_ready_event_);
        return 1;
    }
    hook_window_ = CreateWindowExW(
        0, hook_window_class_name, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    if (!hook_window_) {
        last_error_ = GetLastError();
        SetEvent(hook_ready_event_);
        return 2;
    }

    owner_ = this;
    hook_ = SetWindowsHookEx(
        WH_KEYBOARD_LL, &KeyboardManager::keyboard_hook,
        GetModuleHandle(NULL), NULL);
    if (!hook_) {
        last_error_ = GetLastError();
        owner_ = nullptr;
        DestroyWindow(hook_window_);
        SetEvent(hook_ready_event_);
        return 3;
    }
    last_error_ = ERROR_SUCCESS;
    SetEvent(hook_ready_event_);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    (void)send_windows_transition(
        windows_hotkey_state_.restore_suppressed_windows());
    UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
    hook_window_ = nullptr;
    if (owner_ == this) owner_ = nullptr;
    return 0;
}

bool KeyboardManager::send_control(
    const UINT message, const WPARAM wparam, const LPARAM lparam,
    DWORD_PTR* result) noexcept {
    if (!hook_window_) {
        last_error_ = ERROR_NOT_READY;
        return false;
    }
    DWORD_PTR local_result = 0;
    SetLastError(ERROR_SUCCESS);
    if (!SendMessageTimeoutW(
            hook_window_, message, wparam, lparam,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, hook_command_timeout_ms,
            result ? result : &local_result)) {
        last_error_ = GetLastError();
        if (last_error_ == ERROR_SUCCESS) last_error_ = WAIT_TIMEOUT;
        return false;
    }
    last_error_ = ERROR_SUCCESS;
    return true;
}

void KeyboardManager::update(const State& state) noexcept {
    const auto mask = mask_for_state(state);
    if (hook_window_) {
        (void)send_control(update_state_message, mask);
    } else {
        blocked_mask_ = mask;
    }
}

bool KeyboardManager::register_binding(
    const int identifier, const HotKeyBinding& binding) {
    if (!binding.gesture) return true;
    if (!binding.force_override) return register_standard(identifier, *binding.gesture);
    if (!hook_window_
        || std::ranges::find(used_gestures_, *binding.gesture) != used_gestures_.end()) {
        return false;
    }
    RegistrationRequest request{
        .identifier = identifier,
        .gesture = *binding.gesture,
    };
    DWORD_PTR result = 0;
    if (!send_control(
            add_registration_message, 0,
            reinterpret_cast<LPARAM>(&request), &result)
        || result == 0 || !request.accepted) {
        return false;
    }
    used_gestures_.push_back(*binding.gesture);
    return true;
}

bool KeyboardManager::register_standard(
    const int identifier, const HotKeyGesture& gesture) {
    if (std::ranges::find(used_gestures_, gesture) != used_gestures_.end()) return false;
    if (!RegisterHotKey(target_window_, identifier, gesture.modifiers | MOD_NOREPEAT,
                        gesture.virtual_key)) {
        return false;
    }
    standard_identifiers_.push_back(identifier);
    used_gestures_.push_back(gesture);
    return true;
}

void KeyboardManager::unregister_all() noexcept {
    for (const auto identifier : standard_identifiers_) {
        UnregisterHotKey(target_window_, identifier);
    }
    standard_identifiers_.clear();
    used_gestures_.clear();
    if (hook_window_) (void)send_control(clear_registrations_message);
}

bool KeyboardManager::probe_available(const HotKeyGesture& gesture) const noexcept {
    if (!RegisterHotKey(target_window_, probe_identifier,
                        gesture.modifiers | MOD_NOREPEAT,
                        gesture.virtual_key)) {
        return false;
    }
    UnregisterHotKey(target_window_, probe_identifier);
    return true;
}

bool KeyboardManager::begin_capture(CaptureHandler handler) noexcept {
    if (!hook_window_ || !capture_window_) {
        last_error_ = ERROR_NOT_READY;
        return false;
    }
    end_capture();
    ++capture_session_;
    if (capture_session_ == 0) ++capture_session_;
    capture_handler_ = std::move(handler);
    capture_active_ = true;
    DWORD_PTR result = 0;
    if (!send_control(begin_capture_message, capture_session_, 0, &result)
        || result == 0) {
        capture_active_ = false;
        capture_handler_ = {};
        return false;
    }
    return true;
}

void KeyboardManager::end_capture() noexcept {
    if (capture_active_ && hook_window_) {
        (void)send_control(end_capture_message, capture_session_);
    }
    capture_active_ = false;
    capture_handler_ = {};
}

DWORD KeyboardManager::last_error() const noexcept {
    return last_error_;
}

void KeyboardManager::stop() noexcept {
    end_capture();
    if (hook_window_) (void)send_control(shutdown_hook_message);
    if (hook_thread_) {
        (void)WaitForSingleObject(hook_thread_, hook_start_timeout_ms);
        CloseHandle(hook_thread_);
        hook_thread_ = nullptr;
    }
    if (hook_ready_event_) {
        CloseHandle(hook_ready_event_);
        hook_ready_event_ = nullptr;
    }
    hook_thread_id_ = 0;
    if (capture_window_) {
        DestroyWindow(capture_window_);
        capture_window_ = nullptr;
    }
    target_window_ = nullptr;
}

LRESULT CALLBACK KeyboardManager::hook_window_procedure(
    const HWND window, const UINT message,
    const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* manager = static_cast<KeyboardManager*>(creation->lpCreateParams);
        manager->hook_window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(manager));
    }
    auto* manager = reinterpret_cast<KeyboardManager*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    return manager ? manager->handle_hook_window_message(message, wparam, lparam)
                   : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT KeyboardManager::handle_hook_window_message(
    const UINT message, const WPARAM wparam, const LPARAM lparam) noexcept {
    if (message == update_state_message) {
        blocked_mask_ = static_cast<std::uint32_t>(wparam);
        return TRUE;
    }
    if (message == add_registration_message) {
        auto* request = reinterpret_cast<RegistrationRequest*>(lparam);
        if (!request) return FALSE;
        if (const auto index = windows_letter_hotkey_index(request->gesture)) {
            if (!is_supported_windows_letter_hotkey(request->gesture)) return FALSE;
            windows_override_mask_ |= std::uint32_t{1} << *index;
            windows_override_identifiers_[*index] = request->identifier;
        } else {
            try {
                forced_registrations_.push_back(
                    {request->identifier, request->gesture, false});
            } catch (...) {
                return FALSE;
            }
        }
        request->accepted = true;
        return TRUE;
    }
    if (message == clear_registrations_message) {
        forced_registrations_.clear();
        pressed_keys_.fill(false);
        windows_override_mask_ = 0;
        windows_override_identifiers_.fill(0);
        windows_override_keys_down_.fill(false);
        return TRUE;
    }
    if (message == begin_capture_message) {
        if (wparam == 0) return FALSE;
        hook_capture_session_ = static_cast<UINT>(wparam);
        recording_state_->state.SetRecording(true);
        return TRUE;
    }
    if (message == end_capture_message) {
        if (wparam == 0 || static_cast<UINT>(wparam) == hook_capture_session_) {
            recording_state_->state.SetRecording(false);
        }
        return TRUE;
    }
    if (message == shutdown_hook_message) {
        DestroyWindow(hook_window_);
        return TRUE;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hook_window_, message, wparam, lparam);
}

LRESULT CALLBACK KeyboardManager::capture_window_procedure(
    const HWND window, const UINT message,
    const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* manager = static_cast<KeyboardManager*>(creation->lpCreateParams);
        manager->capture_window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(manager));
    }
    auto* manager = reinterpret_cast<KeyboardManager*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    return manager ? manager->handle_capture_window_message(message, wparam, lparam)
                   : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT KeyboardManager::handle_capture_window_message(
    const UINT message, const WPARAM wparam, const LPARAM lparam) noexcept {
    if (message == capture_result_message) {
        if (!capture_active_ || static_cast<UINT>(wparam) != capture_session_) return 0;
        const auto result = unpack_capture_result(lparam);
        capture_active_ = false;
        auto handler = std::move(capture_handler_);
        capture_handler_ = {};
        if (handler) {
            try {
                handler(result);
            } catch (...) {
            }
        }
        return 0;
    }
    return DefWindowProcW(capture_window_, message, wparam, lparam);
}

LRESULT KeyboardManager::handle_capture_event(
    const WPARAM message, KBDLLHOOKSTRUCT& event) noexcept {
    LowlevelKeyboardEvent low_level_event{
        .lParam = &event,
        .wParam = message,
    };
    const auto decision = recording_state_->state.DetectShortcutUIBackend(
        &low_level_event);
    if (decision == Helpers::KeyboardHookDecision::Suppress) {
        Shortcut shortcut;
        if (recording_state_->state.TakeCompletedShortcut(shortcut)) {
            const auto modifiers = shortcut_modifiers(shortcut);
            const auto kind = shortcut.actionKey == VK_ESCAPE && modifiers == 0
                ? KeyboardCaptureResultKind::cancelled
                : KeyboardCaptureResultKind::captured;
            (void)PostMessageW(
                capture_window_, capture_result_message, hook_capture_session_,
                pack_capture_result(kind, modifiers, shortcut.actionKey));
        }
        return 1;
    }
    return 0;
}

LRESULT KeyboardManager::handle_registered_hotkey(
    const WPARAM message, const KBDLLHOOKSTRUCT& event) noexcept {
    if ((event.flags & LLKHF_INJECTED) != 0) return 0;
    const auto key_down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const auto key_up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!key_down && !key_up) return 0;
    if (event.vkCode < pressed_keys_.size()) {
        if (key_down) pressed_keys_[event.vkCode] = true;
        if (key_up) pressed_keys_[event.vkCode] = false;
    }
    if (windows_override_mask_ != 0 && event.vkCode >= L'A' && event.vkCode <= L'Z') {
        const auto index = static_cast<std::size_t>(event.vkCode - L'A');
        const auto identifier = windows_override_identifiers_[index];
        const auto selected = ((windows_override_mask_ >> index) & 1U) != 0;
        if (identifier != 0 && selected && key_down
            && current_modifiers(pressed_keys_) == MOD_WIN
            && !windows_override_keys_down_[index]) {
            windows_override_keys_down_[index] = true;
            PostMessageW(target_window_, WM_HOTKEY, static_cast<WPARAM>(identifier),
                         MAKELPARAM(MOD_WIN, event.vkCode));
        }
        if (key_up) windows_override_keys_down_[index] = false;
    }
    for (auto& registration : forced_registrations_) {
        if (registration.gesture.virtual_key != event.vkCode) continue;
        if (key_down) {
            if (current_modifiers(pressed_keys_) != registration.gesture.modifiers) continue;
            if (!registration.key_is_down) {
                registration.key_is_down = true;
                keybd_event(VK_CANCEL, 0, 0, 0);
                keybd_event(VK_CANCEL, 0, KEYEVENTF_KEYUP, 0);
                PostMessageW(target_window_, WM_HOTKEY,
                             static_cast<WPARAM>(registration.identifier),
                             MAKELPARAM(registration.gesture.modifiers,
                                        registration.gesture.virtual_key));
            }
            return 1;
        }
        if (registration.key_is_down) {
            registration.key_is_down = false;
            return 1;
        }
    }
    return 0;
}

LRESULT KeyboardManager::handle_key(
    const WPARAM message, const KBDLLHOOKSTRUCT& event) noexcept {
    if (event.dwExtraInfo == simpilot_injected_event) return 0;
    const auto key_down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const auto key_up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!key_down && !key_up) return 0;

    const auto action = windows_hotkey_state_.handle(
        event.vkCode, key_down, key_up, blocked_mask_,
        current_modifiers(pressed_keys_) == MOD_WIN);
    if (!send_windows_transition(action)
        && action.decision == WindowsHotKeyDecision::block_and_release_windows) {
        windows_hotkey_state_.cancel_suppression(
            action.left_windows, action.right_windows);
    }
    return action.decision == WindowsHotKeyDecision::block
            || action.decision == WindowsHotKeyDecision::block_and_release_windows
        ? 1 : 0;
}

bool KeyboardManager::send_windows_transition(
    const WindowsHotKeyTransition& action) noexcept {
    if (action.decision != WindowsHotKeyDecision::block_and_release_windows
        && action.decision != WindowsHotKeyDecision::pass_and_restore_windows) {
        return true;
    }

    INPUT inputs[4]{};
    UINT count = 0;
    const auto add_key = [&inputs, &count](const WORD virtual_key, const DWORD flags) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = virtual_key;
        inputs[count].ki.dwFlags = flags;
        inputs[count].ki.dwExtraInfo = simpilot_injected_event;
        ++count;
    };
    if (action.decision == WindowsHotKeyDecision::block_and_release_windows) {
        add_key(0xFF, 0);
        add_key(0xFF, KEYEVENTF_KEYUP);
    }
    const auto flags = action.decision == WindowsHotKeyDecision::block_and_release_windows
        ? KEYEVENTF_KEYUP : DWORD{0};
    if (action.left_windows) add_key(VK_LWIN, flags);
    if (action.right_windows) add_key(VK_RWIN, flags);
    return count == 0 || SendInput(count, inputs, sizeof(INPUT)) == count;
}

LRESULT CALLBACK KeyboardManager::keyboard_hook(
    const int code, const WPARAM wparam, const LPARAM lparam) {
    auto* manager = owner_;
    if (code == HC_ACTION && manager) {
        auto& event = *reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
        if (manager->handle_capture_event(wparam, event) != 0) return 1;
        if (manager->handle_registered_hotkey(wparam, event) != 0) return 1;
        if (manager->handle_key(wparam, event) != 0) return 1;
    }
    return CallNextHookEx(manager ? manager->hook_ : nullptr, code, wparam, lparam);
}

} // namespace simpilot
