#pragma once

#include "keyboard_mapping_editor_model.hpp"

#include "simpilot/localization.hpp"

#include <Windows.h>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace simpilot {

// The editor deliberately depends on small callbacks instead of the keyboard
// thread implementation. KeyboardManager can adapt its capture result to
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
    struct KeyOption {
        PhysicalKey key;
        bool recorded = false;
    };

    KeyboardMappingDialog(HINSTANCE instance, HWND owner, std::string language_code,
                          const KeyboardMappingRule* initial,
                          CaptureCallbacks callbacks, DiagnosticSink diagnostic_sink);

    [[nodiscard]] std::optional<KeyboardMappingRule> run();
    void create_controls();
    void create_key_options();
    void update_fonts();
    void layout_controls(int width, int height);
    void populate_modifier_combo(HWND combo);
    void populate_action_combo(
        HWND combo, bool optional, const std::vector<KeyOption>& options);
    void ensure_model_options();
    std::size_t ensure_modifier_option(const PhysicalKey& key, bool recorded);
    std::size_t ensure_source_action_option(
        const PhysicalKey& key, bool recorded);
    std::size_t ensure_action_option(const PhysicalKey& key, bool recorded);
    void append_modifier_option_to_controls(std::size_t index);
    void append_source_action_option_to_control(std::size_t index);
    void append_action_option_to_controls(std::size_t index);
    void select_combo_key(HWND combo, const std::vector<KeyOption>& options,
                          const std::optional<PhysicalKey>& key);
    [[nodiscard]] std::optional<PhysicalKey> combo_key(
        HWND combo, const std::vector<KeyOption>& options) const noexcept;
    void sync_controls_from_model();
    void sync_model_from_controls();
    void update_previews();
    void update_chord_availability();
    void handle_combo_change(int identifier);
    void begin_trigger_capture();
    void begin_output_capture();
    void end_capture();
    void save();
    void use_foreground_process();
    void show_draft_error(KeyboardMappingDraftError error) const;
    [[nodiscard]] std::wstring key_label(const PhysicalKey& key) const;
    [[nodiscard]] std::wstring option_label(const KeyOption& option) const;
    [[nodiscard]] std::wstring source_preview() const;
    [[nodiscard]] std::wstring target_preview() const;
    [[nodiscard]] const wchar_t* text(std::string_view key) const noexcept;
    void diagnose(std::wstring_view message) const noexcept;

    static LRESULT CALLBACK window_procedure(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND owner_;
    Localization localization_;
    std::optional<KeyboardMappingRule> initial_;
    KeyboardMappingEditorModel editor_;
    CaptureCallbacks callbacks_;
    DiagnosticSink diagnostic_sink_;
    std::vector<KeyOption> modifier_options_;
    std::vector<KeyOption> source_action_options_;
    std::vector<KeyOption> action_options_;
    HWND window_ = nullptr;
    std::optional<KeyboardMappingRule> result_;
    bool capturing_ = false;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HFONT section_font_ = nullptr;
    HFONT title_font_ = nullptr;
    HWND title_ = nullptr;
    HWND source_heading_ = nullptr;
    HWND source_hint_ = nullptr;
    HWND source_summary_ = nullptr;
    HWND source_record_ = nullptr;
    HWND source_modifiers_label_ = nullptr;
    std::array<HWND, 4> source_modifier_combos_{};
    HWND source_action_label_ = nullptr;
    HWND source_action_combo_ = nullptr;
    HWND source_chord_label_ = nullptr;
    HWND source_chord_combo_ = nullptr;
    HWND target_heading_ = nullptr;
    HWND target_hint_ = nullptr;
    HWND target_summary_ = nullptr;
    HWND target_record_ = nullptr;
    HWND target_modifiers_label_ = nullptr;
    std::array<HWND, 4> target_modifier_combos_{};
    HWND target_action_label_ = nullptr;
    HWND target_action_combo_ = nullptr;
    HWND divider_ = nullptr;
    HWND process_label_ = nullptr;
    HWND process_edit_ = nullptr;
    HWND process_foreground_ = nullptr;
    HWND exact_match_ = nullptr;
    HWND enabled_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;
};

} // namespace simpilot
