#pragma once

#include "simpilot/localization.hpp"
#include "simpilot/program_resolver.hpp"

#include <Windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace simpilot {

class ProgramSelectionDialog final {
public:
    [[nodiscard]] static std::optional<std::filesystem::path> show_modal(
        HINSTANCE instance, HWND owner, std::string language_code,
        std::wstring executable, const std::vector<ProgramCandidate>& candidates);
    [[nodiscard]] static std::optional<std::filesystem::path> show_modal(
        HINSTANCE instance, HWND owner, UiLanguage language,
        std::wstring executable, const std::vector<ProgramCandidate>& candidates) {
        return show_modal(instance, owner,
                          std::string(Localization::language_code(language)),
                          std::move(executable), candidates);
    }

private:
    ProgramSelectionDialog(HINSTANCE instance, HWND owner, std::string language_code,
                           std::wstring executable,
                           const std::vector<ProgramCandidate>& candidates);

    [[nodiscard]] std::optional<std::filesystem::path> run();
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
    std::wstring executable_;
    const std::vector<ProgramCandidate>& candidates_;
    HWND window_ = nullptr;
    HWND prompt_ = nullptr;
    HWND list_ = nullptr;
    HWND select_button_ = nullptr;
    HWND cancel_button_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    std::optional<std::filesystem::path> result_;
};

} // namespace simpilot
