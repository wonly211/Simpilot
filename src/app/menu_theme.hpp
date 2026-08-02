#pragma once

#include "simpilot/app_settings.hpp"

namespace simpilot {

class MenuThemeController final {
public:
    [[nodiscard]] static bool apply(MenuTheme theme) noexcept;
    [[nodiscard]] static bool dark_mode_enabled(MenuTheme theme) noexcept;
};

} // namespace simpilot
