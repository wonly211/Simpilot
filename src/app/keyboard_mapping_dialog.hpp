#pragma once

#include "simpilot/keyboard_mapping.hpp"
#include "simpilot/localization.hpp"

#include <Windows.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace simpilot {

// The editor deliberately depends on small callbacks instead of the keyboard
// thread implementation.  KeyboardManager can adapt its capture result to
// these callbacks without making the settings window own hook-thread state.
class KeyboardMappingDialog final {
public:
    using TriggerCaptureHandler = std::function<void(std::optional<KeyboardTrigger>)>;
    using OutputCaptureHandler = std::function<void(std::optional<KeyboardOutput>)>;
    using BeginTriggerCapture = std::function<bool(TriggerCaptureHandler)>;
    using BeginOutputCapture = std::function<bool(OutputCaptureHandler)>;
    using EndCapture = std::function<void()>;
    using ForegroundProcessProvider =
        std::function<std::optional<std::wstring>()>;
    using DiagnosticSink = std::function<void(std::wstring_view)>;

    struct CaptureCallbacks {
        BeginTriggerCapture begin_trigger;
        BeginOutputCapture begin_output;
        EndCapture end;
        ForegroundProcessProvider foreground_process;
    };

    [[nodiscard]] static std::optional<KeyboardMappingRule> show_modal(
        HINSTANCE instance, HWND owner, std::string language_code,
        const KeyboardMappingRule* initial = nullptr,
        CaptureCallbacks callbacks = {}, DiagnosticSink diagnostic_sink = {});

private:
    KeyboardMappingDialog(HINSTANCE instance, HWND owner, std::string language_code,
                          const KeyboardMappingRule* initial,
                          CaptureCallbacks callbacks, DiagnosticSink diagnostic_sink);

    [[nodiscard]] std::optional<KeyboardMappingRule> run();
    void create_controls();
    void update_fonts();
    void layout_controls(int width, int height);
    void update_source_text();
    void update_target_text();
    void begin_trigger_capture();
    void begin_output_capture();
    void end_capture();
    void save();
    void use_foreground_process();
    [[nodiscard]] std::optional<KeyboardMappingRule> read_rule() const;
    [[nodiscard]] const wchar_t* text(std::string_view key) const noexcept;
    void diagnose(std::wstring_view message) const noexcept;

    static LRESULT CALLBACK window_procedure(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND owner_;
    Localization localization_;
    std::optional<KeyboardMappingRule> initial_;
    CaptureCallbacks callbacks_;
    DiagnosticSink diagnostic_sink_;
    HWND window_ = nullptr;
    std::optional<KeyboardMappingRule> result_;
    bool capturing_ = false;
    bool capturing_output_ = false;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HFONT section_font_ = nullptr;
    HFONT title_font_ = nullptr;
    HWND title_ = nullptr;
    HWND source_heading_ = nullptr;
    HWND source_hint_ = nullptr;
    HWND source_edit_ = nullptr;
    HWND source_record_ = nullptr;
    HWND target_heading_ = nullptr;
    HWND target_hint_ = nullptr;
    HWND target_edit_ = nullptr;
    HWND target_record_ = nullptr;
    HWND process_label_ = nullptr;
    HWND process_edit_ = nullptr;
    HWND process_foreground_ = nullptr;
    HWND exact_match_ = nullptr;
    HWND enabled_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;
};

} // namespace simpilot
