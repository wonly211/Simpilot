#pragma once

#include "simpilot/app_module.hpp"
#include "simpilot/settings_registry.hpp"

namespace simpilot {

struct MouseLocatorSettings {
    bool enabled = false;
    [[nodiscard]] static MouseLocatorSettings read(const SettingsDocument& document);
    void write(SettingsDocument& document) const;
    bool operator==(const MouseLocatorSettings&) const = default;
};

[[nodiscard]] std::unique_ptr<IAppModule> make_mouse_locator_module(
    HINSTANCE instance, const SettingsDocument& document, SettingsRegistry& pages,
    SettingsParticipantRegistry& participants,
    std::function<void(std::wstring_view)> diagnose);

} // namespace simpilot
