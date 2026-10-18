#pragma once

#include "input_method_module.hpp"
#include "input_method_backend.hpp"

namespace simpilot::input_method {

// Private test seam: tests can verify the real module/transactions without
// sending input-language requests to applications on the user's desktop.
std::unique_ptr<IAppModule> make_module(
    std::filesystem::path config_directory, UiDispatcher& dispatcher,
    SettingsRegistry& pages, SettingsParticipantRegistry& participants,
    std::function<void(std::wstring_view)> diagnose, std::unique_ptr<IBackend> backend);

} // namespace simpilot::input_method
