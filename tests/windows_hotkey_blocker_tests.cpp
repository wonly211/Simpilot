#include "keyboard_manager.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using simpilot::WindowsHotKeyDecision;
using simpilot::WindowsHotKeyTransition;

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_transition(
    const WindowsHotKeyTransition& actual,
    const WindowsHotKeyDecision decision,
    const bool left_windows,
    const bool right_windows,
    const char* message) {
    require(actual.decision == decision
            && actual.left_windows == left_windows
            && actual.right_windows == right_windows,
            message);
}

constexpr std::uint32_t mask_for(const wchar_t letter) {
    return std::uint32_t{1} << static_cast<unsigned int>(letter - L'A');
}

void blocks_and_logically_releases_windows() {
    simpilot::WindowsHotKeyState state;
    const auto mask = mask_for(L'G');
    require_transition(state.handle(VK_LWIN, true, false, mask),
        WindowsHotKeyDecision::pass, false, false,
        "Win key down must initially pass through");
    require_transition(state.handle(L'G', true, false, mask),
        WindowsHotKeyDecision::block_and_release_windows, true, false,
        "First Win+G key down must release the logical Win state");
    require_transition(state.handle(L'G', true, false, mask),
        WindowsHotKeyDecision::block, false, false,
        "Win+G auto-repeat must remain blocked");
    require_transition(state.handle(L'G', false, true, mask),
        WindowsHotKeyDecision::block, false, false,
        "Blocked G key up must remain blocked");
    require_transition(state.handle(VK_LWIN, false, true, mask),
        WindowsHotKeyDecision::block, false, false,
        "Physical Win key up must be swallowed after an injected Win key up");
}

void supports_right_windows_and_release_order() {
    simpilot::WindowsHotKeyState state;
    const auto mask = mask_for(L'Z');
    (void)state.handle(VK_RWIN, true, false, mask);
    require_transition(state.handle(L'Z', true, false, mask),
        WindowsHotKeyDecision::block_and_release_windows, false, true,
        "Right Win+Z must release the logical right Win state");
    require_transition(state.handle(VK_RWIN, false, true, mask),
        WindowsHotKeyDecision::block, false, false,
        "Win released before action key must remain swallowed");
    require_transition(state.handle(L'Z', false, true, mask),
        WindowsHotKeyDecision::block, false, false,
        "Action key up must remain blocked after Win is released first");
}

void restores_windows_before_an_unblocked_key() {
    simpilot::WindowsHotKeyState state;
    const auto mask = mask_for(L'G');
    (void)state.handle(VK_LWIN, true, false, mask);
    (void)state.handle(L'G', true, false, mask);
    (void)state.handle(L'G', false, true, mask);
    require_transition(state.handle(L'H', true, false, mask),
        WindowsHotKeyDecision::pass_and_restore_windows, true, false,
        "An unblocked key must restore held Win before passing through");
    require_transition(state.handle(L'H', false, true, mask),
        WindowsHotKeyDecision::pass, false, false,
        "Unblocked key up must pass through");
    require_transition(state.handle(VK_LWIN, false, true, mask),
        WindowsHotKeyDecision::pass, false, false,
        "Physical Win key up must pass after logical Win was restored");
}

void keeps_multiple_selected_keys_blocked_while_win_is_held() {
    simpilot::WindowsHotKeyState state;
    const auto mask = mask_for(L'F') | mask_for(L'G');
    (void)state.handle(VK_LWIN, true, false, mask);
    (void)state.handle(L'G', true, false, mask);
    (void)state.handle(L'G', false, true, mask);
    require_transition(state.handle(L'F', true, false, mask),
        WindowsHotKeyDecision::block, false, false,
        "A second selected shortcut must remain blocked while Win is held");
    require_transition(state.handle(L'F', false, true, mask),
        WindowsHotKeyDecision::block, false, false,
        "Second selected action key up must remain blocked");
    require_transition(state.handle(VK_LWIN, false, true, mask),
        WindowsHotKeyDecision::block, false, false,
        "Suppressed physical Win key up must remain blocked");
}

void does_not_begin_blocking_mid_key_press() {
    simpilot::WindowsHotKeyState state;
    const auto mask = mask_for(L'F');
    require_transition(state.handle(L'F', true, false, mask),
        WindowsHotKeyDecision::pass, false, false,
        "F pressed before Win must pass through");
    (void)state.handle(VK_LWIN, true, false, mask);
    require_transition(state.handle(L'F', true, false, mask),
        WindowsHotKeyDecision::pass, false, false,
        "F repeat must pass when its initial key down passed through");
    require_transition(state.handle(L'F', false, true, mask),
        WindowsHotKeyDecision::pass, false, false,
        "F key up must pass when its key down passed through");
    (void)state.handle(VK_LWIN, false, true, mask);
}

void passes_selected_key_with_an_additional_modifier() {
    simpilot::WindowsHotKeyState state;
    const auto mask = mask_for(L'Z');
    (void)state.handle(VK_LWIN, true, false, mask);
    require_transition(state.handle(L'Z', true, false, mask, false),
        WindowsHotKeyDecision::pass, false, false,
        "Shift+Win+Z must not be treated as the blocked Win+Z system shortcut");
    require_transition(state.handle(L'Z', false, true, mask, false),
        WindowsHotKeyDecision::pass, false, false,
        "Shift+Win+Z key up must remain available to registered global hotkeys");
    require_transition(state.handle(VK_LWIN, false, true, mask),
        WindowsHotKeyDecision::pass, false, false,
        "Win key up must pass after an additional-modifier shortcut");
}

} // namespace

int wmain() {
    try {
        blocks_and_logically_releases_windows();
        supports_right_windows_and_release_order();
        restores_windows_before_an_unblocked_key();
        keeps_multiple_selected_keys_blocked_while_win_is_held();
        does_not_begin_blocking_mid_key_press();
        passes_selected_key_with_an_additional_modifier();
        std::wcout << L"All Windows hotkey blocker tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
