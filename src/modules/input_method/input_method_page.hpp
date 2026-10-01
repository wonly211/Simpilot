#pragma once

#include "input_method_model.hpp"
#include "simpilot/settings_registry.hpp"

#include <functional>

namespace simpilot::input_method {

struct PageServices {
    std::function<const std::vector<Profile>&()> profiles;
    std::function<void()> refresh;
    std::function<Status()> status;
};

std::unique_ptr<ISettingsPage> make_settings_page(Settings& draft, PageServices services);
std::optional<Rule> show_rule_dialog(
    HINSTANCE instance, HWND owner, const Localization& localization,
    Rule rule, const std::vector<Profile>& profiles,
    const std::vector<Rule>& other_rules);

} // namespace simpilot::input_method
