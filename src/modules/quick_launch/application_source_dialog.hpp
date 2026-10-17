#pragma once

#include "simpilot/localization.hpp"
#include "settings_visual_style.hpp"

#include <Windows.h>

#include <optional>
#include <string>

namespace simpilot {

enum class ApplicationSource {
    packaged,
    executable,
    apps_folder,
};

class ApplicationSourceDialog final {
public:
    [[nodiscard]] static std::optional<ApplicationSource> show_modal(
        HINSTANCE instance, HWND owner, std::string language_code);

private:
    ApplicationSourceDialog(HINSTANCE instance, HWND owner, std::string language_code);

    [[nodiscard]] std::optional<ApplicationSource> run();
    void create_controls();
    void update_typography();
    void layout_controls(int width, int height);
    void accept_selection();
    [[nodiscard]] const wchar_t* text(std::string_view key) const noexcept;

    static LRESULT CALLBACK window_procedure(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND owner_;
    Localization localization_;
    HWND window_ = nullptr;
    HWND title_ = nullptr;
    HWND subtitle_ = nullptr;
    HWND packaged_radio_ = nullptr;
    HWND packaged_description_ = nullptr;
    HWND executable_radio_ = nullptr;
    HWND executable_description_ = nullptr;
    HWND apps_folder_radio_ = nullptr;
    HWND apps_folder_description_ = nullptr;
    HWND continue_button_ = nullptr;
    HWND cancel_button_ = nullptr;
    HFONT body_font_ = nullptr;
    settings_visual_style::PageTypography typography_;
    UINT dpi_ = 96;
    std::optional<ApplicationSource> result_;
};

} // namespace simpilot
