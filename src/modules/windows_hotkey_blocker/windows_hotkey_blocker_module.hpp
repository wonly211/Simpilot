#pragma once

#include "simpilot/app_module.hpp"
#include "simpilot/hotkey_registry.hpp"
#include "simpilot/settings_registry.hpp"

#include <array>

namespace simpilot {

struct WindowsHotkeyBlockingSettings {
    std::array<bool, 26> disabled{};
    [[nodiscard]] static WindowsHotkeyBlockingSettings read(const SettingsDocument& document);
    void write(SettingsDocument& document) const;
    bool operator==(const WindowsHotkeyBlockingSettings&) const = default;
};

[[nodiscard]] std::unique_ptr<IAppModule> make_windows_hotkey_blocker_module(
    const SettingsDocument& document, SettingsRegistry& pages,
    SettingsParticipantRegistry& participants, HotkeyRegistry& hotkeys,
    std::function<bool(const std::array<bool, 26>&)> apply_policy);

} // namespace simpilot
