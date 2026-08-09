#pragma once

#include "simpilot/localization.hpp"

#include <Windows.h>

#include <array>
#include <filesystem>
#include <string>

namespace simpilot {

inline constexpr wchar_t about_window_class_name[] = L"Simpilot.AboutWindow";
inline constexpr wchar_t about_simplified_chinese_manual_url[] =
    L"https://github.com/wonly211/Simpilot/blob/main/docs/zh-CN/"
    L"%E7%94%A8%E6%88%B7%E6%89%8B%E5%86%8C.md";
inline constexpr wchar_t about_english_manual_url[] =
    L"https://github.com/wonly211/Simpilot/blob/main/docs/en-US/User-Manual.md";

[[nodiscard]] inline constexpr const wchar_t* about_manual_url_for_language(
    const LANGID language) noexcept {
    return PRIMARYLANGID(language) == LANG_CHINESE
        ? about_simplified_chinese_manual_url
        : about_english_manual_url;
}

class AboutWindow final {
public:
    static void show_modal(HINSTANCE instance, HWND owner,
                           const Localization& localization,
                           std::filesystem::path executable_path,
                           std::wstring version);

private:
    AboutWindow(HINSTANCE instance, HWND owner, const Localization& localization,
                std::filesystem::path executable_path, std::wstring version);

    void run();
    void create_controls();
    void update_fonts();
    void layout_controls(int width, int height);
    void scroll_to(int position);
    void open_target(int identifier);
    [[nodiscard]] const wchar_t* text(AboutText identifier) const noexcept;

    static LRESULT CALLBACK window_procedure(HWND window, UINT message,
                                             WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND owner_;
    const Localization& localization_;
    std::filesystem::path executable_path_;
    std::wstring version_;
    HWND window_ = nullptr;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HFONT semibold_font_ = nullptr;
    HFONT section_font_ = nullptr;
    HFONT title_font_ = nullptr;
    int scroll_position_ = 0;
    int content_height_ = 0;
    std::array<int, 2> divider_positions_{};
    HWND icon_ = nullptr;
    HWND product_name_ = nullptr;
    HWND positioning_ = nullptr;
    HWND version_label_ = nullptr;
    HWND tagline_ = nullptr;
    std::array<HWND, 4> primary_links_{};
    HWND product_information_ = nullptr;
    std::array<HWND, 3> information_labels_{};
    std::array<HWND, 3> information_values_{};
    HWND local_data_ = nullptr;
    HWND third_party_summary_ = nullptr;
    std::array<HWND, 2> legal_links_{};
    HWND close_button_ = nullptr;
};

} // namespace simpilot
