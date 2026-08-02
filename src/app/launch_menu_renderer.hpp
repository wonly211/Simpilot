#pragma once

#include "simpilot/app_settings.hpp"

#include <Windows.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace simpilot {

[[nodiscard]] UINT launch_menu_alignment(POINT cursor, RECT work_area,
                                         SIZE menu_size) noexcept;

class LaunchMenuRenderer final {
public:
    LaunchMenuRenderer() = default;
    ~LaunchMenuRenderer();

    LaunchMenuRenderer(const LaunchMenuRenderer&) = delete;
    LaunchMenuRenderer& operator=(const LaunchMenuRenderer&) = delete;

    void begin(MenuTheme theme, UINT dpi);
    void end() noexcept;
    [[nodiscard]] bool append(HMENU menu, UINT command_id, std::wstring_view text,
                              HICON icon, HMENU submenu = nullptr);
    [[nodiscard]] bool append_separator(HMENU menu);
    [[nodiscard]] bool measure(MEASUREITEMSTRUCT& measurement) const noexcept;
    [[nodiscard]] SIZE measure_menu(HMENU menu) const noexcept;
    [[nodiscard]] bool draw(const DRAWITEMSTRUCT& drawing) const noexcept;

private:
    struct Item {
        std::wstring text;
        HICON icon = nullptr;
        bool submenu = false;
        bool separator = false;
    };

    [[nodiscard]] int scale(int value) const noexcept;

    std::vector<std::unique_ptr<Item>> items_;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    bool dark_ = false;
    bool high_contrast_ = false;
};

} // namespace simpilot
