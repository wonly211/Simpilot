#pragma once

#include "simpilot/app_settings.hpp"
#include "simpilot/localization.hpp"
#include "keyboard_manager.hpp"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

namespace simpilot {

class CustomHotKeyDialog final {
public:
    using DiagnosticSink = std::function<void(std::wstring_view)>;

    [[nodiscard]] static std::optional<CustomGlobalHotKey> show_modal(
        HINSTANCE instance, HWND owner, UiLanguage language,
        KeyboardManager& keyboard_manager,
        std::filesystem::path config_directory,
        const CustomGlobalHotKey* initial = nullptr,
        DiagnosticSink diagnostic_sink = {});

private:
    CustomHotKeyDialog(HINSTANCE instance, HWND owner, UiLanguage language,
                       KeyboardManager& keyboard_manager,
                       std::filesystem::path config_directory,
                       const CustomGlobalHotKey* initial,
                       DiagnosticSink diagnostic_sink);

    [[nodiscard]] std::optional<CustomGlobalHotKey> run();
    void create_controls();
    void update_fonts();
    void layout_controls(int width, int height);
    void begin_capture();
    void cancel_capture();
    void complete_capture(const HotKeyGesture& gesture);
    void handle_capture_result(const KeyboardCaptureResult& result);
    void update_capture_button();
    void update_action_controls();
    void browse_target();
    void browse_working_directory();
    void update_save_state();
    void save();
    [[nodiscard]] std::filesystem::path resolved_target() const;
    [[nodiscard]] const wchar_t* text(CustomHotKeyText identifier) const noexcept;
    [[nodiscard]] const wchar_t* text(std::string_view key) const noexcept;
    void diagnose_capture(std::wstring_view reason) const noexcept;

    static LRESULT CALLBACK window_procedure(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    HINSTANCE instance_;
    HWND owner_;
    Localization localization_;
    KeyboardManager& keyboard_manager_;
    std::filesystem::path config_directory_;
    DiagnosticSink diagnostic_sink_;
    std::optional<CustomGlobalHotKey> initial_;
    HWND window_ = nullptr;
    std::optional<CustomGlobalHotKey> result_;
    std::optional<HotKeyGesture> gesture_;
    bool capturing_ = false;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HFONT section_font_ = nullptr;
    HFONT title_font_ = nullptr;
    HWND title_ = nullptr;
    HWND trigger_heading_ = nullptr;
    HWND trigger_type_ = nullptr;
    HWND capture_button_ = nullptr;
    HWND allow_modifiers_ = nullptr;
    HWND divider_ = nullptr;
    HWND action_heading_ = nullptr;
    HWND action_type_ = nullptr;
    HWND program_label_ = nullptr;
    HWND program_edit_ = nullptr;
    HWND program_browse_ = nullptr;
    HWND arguments_label_ = nullptr;
    HWND arguments_edit_ = nullptr;
    HWND working_directory_label_ = nullptr;
    HWND working_directory_edit_ = nullptr;
    HWND working_directory_browse_ = nullptr;
    HWND identity_label_ = nullptr;
    HWND identity_combo_ = nullptr;
    HWND existing_label_ = nullptr;
    HWND existing_combo_ = nullptr;
    HWND visibility_label_ = nullptr;
    HWND visibility_combo_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;
};

} // namespace simpilot
