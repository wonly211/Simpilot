#include "keyboard_manager.hpp"

#include <iostream>

int wmain() {
    simpilot::KeyboardManager::State state{};
    state[static_cast<std::size_t>(L'G' - L'A')] = true;

    simpilot::KeyboardManager keyboard_manager;
    if (!keyboard_manager.start(nullptr, state)) {
        std::wcerr << L"Dedicated keyboard thread startup failed with Windows error "
                   << keyboard_manager.last_error() << L".\n";
        return 1;
    }

    state[static_cast<std::size_t>(L'G' - L'A')] = false;
    state[static_cast<std::size_t>(L'F' - L'A')] = true;
    keyboard_manager.update(state);
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

    std::wcout << L"Single keyboard thread lifecycle passed without synthetic input.\n";
    return 0;
}
