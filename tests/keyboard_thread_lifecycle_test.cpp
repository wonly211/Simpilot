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
    keyboard_manager.unregister_all();

    std::wcout << L"Single keyboard thread lifecycle passed without synthetic input.\n";
    return 0;
}
