#pragma once

#include "simpilot/app_module.hpp"
#include "simpilot/settings_registry.hpp"
#include "keyboard_mapping.hpp"

namespace simpilot {

class KeyboardManager;

struct KeyboardMappingSettings {
    bool enabled = true;
    std::vector<KeyboardMappingRule> rules;
    [[nodiscard]] static KeyboardMappingSettings read(
        const SettingsDocument& document,
        std::function<void(std::wstring_view)> diagnose = {});
    void write(SettingsDocument& document) const;
    bool operator==(const KeyboardMappingSettings&) const = default;
};

[[nodiscard]] std::unique_ptr<IAppModule> make_keyboard_mapping_module(
    const SettingsDocument& document, SettingsRegistry& pages,
    SettingsParticipantRegistry& participants, KeyboardManager& keyboard,
    std::function<void(std::wstring_view)> diagnose);

} // namespace simpilot
