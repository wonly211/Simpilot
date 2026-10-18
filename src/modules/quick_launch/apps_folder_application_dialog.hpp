#pragma once

#include "apps_folder_application_catalog.hpp"
#include "simpilot/localization.hpp"

#include <Windows.h>

#include <optional>
#include <string>
#include <vector>

namespace simpilot {

class AppsFolderApplicationDialog final {
public:
    [[nodiscard]] static std::optional<std::wstring> show_modal(
        HINSTANCE instance, HWND owner, std::string language_code,
        const std::vector<AppsFolderApplication>& applications,
        AppsFolderApplicationScope scope,
        std::wstring current_target = {});

private:
    AppsFolderApplicationDialog(HINSTANCE instance, HWND owner, std::string language_code,
        const std::vector<AppsFolderApplication>& applications,
        AppsFolderApplicationScope scope, std::wstring current_target);

    [[nodiscard]] std::optional<std::wstring> run();
    void create_controls();
    void update_font();
    void layout_controls(int width, int height);
    void accept_selection();
    [[nodiscard]] const wchar_t* text(std::string_view key) const noexcept;

    static LRESULT CALLBACK window_procedure(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND owner_;
    Localization localization_;
    const std::vector<AppsFolderApplication>& applications_;
    AppsFolderApplicationScope scope_;
    std::wstring current_target_;
    HWND window_ = nullptr;
    HWND prompt_ = nullptr;
    HWND list_ = nullptr;
    HWND select_button_ = nullptr;
    HWND cancel_button_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    std::optional<std::wstring> result_;
};

} // namespace simpilot
