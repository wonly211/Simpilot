#pragma once

#include "simpilot/app_settings.hpp"
#include "simpilot/localization.hpp"
#include "keyboard_manager.hpp"
#include "menu_editor_window.hpp"
#include "menu_icon_cache.hpp"

#include <Windows.h>
#include <commctrl.h>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace simpilot {

struct MenuIconTarget {
    std::wstring menu_name;
    std::wstring display_name;
    std::wstring target;
    std::wstring custom_key;
    std::wstring icon_source;
    MenuEntryKind kind = MenuEntryKind::command;
};

class SettingsWindow final {
public:
    using AvailabilityProbe = std::function<bool(const HotKeyGesture&)>;
    using DiagnosticSink = std::function<void(std::wstring_view)>;
    using IconChangeSink = std::function<void()>;
    using ApplySink = std::function<bool(const AppSettings&)>;
    using MenuChangeSink = std::function<std::vector<MenuIconTarget>()>;
    using LanguageChangeSink = std::function<void(UiLanguage)>;
    using ProgramResolutionLookup = MenuEditorWindow::ProgramResolutionLookup;
    using ProgramResolutionReselect = MenuEditorWindow::ProgramResolutionReselect;

    [[nodiscard]] static std::optional<AppSettings> show_modal(
        HINSTANCE instance, HWND owner, const AppSettings& current,
        UiLanguage language, KeyboardManager& keyboard_manager,
        AvailabilityProbe availability_probe,
        DiagnosticSink diagnostic_sink, std::vector<MenuIconTarget> menu_icon_targets,
        std::filesystem::path config_directory, std::filesystem::path icon_cache_directory,
        IconChangeSink icon_change_sink = {},
        ApplySink apply_sink = {}, MenuChangeSink menu_change_sink = {},
        LanguageChangeSink language_change_sink = {},
        ProgramResolutionLookup resolution_lookup = {},
        ProgramResolutionReselect resolution_reselect = {});

private:
    SettingsWindow(HINSTANCE instance, HWND owner, AppSettings current,
                   UiLanguage language, KeyboardManager& keyboard_manager,
                   AvailabilityProbe availability_probe,
                   DiagnosticSink diagnostic_sink,
                   std::vector<MenuIconTarget> menu_icon_targets,
                   std::filesystem::path config_directory,
                   std::filesystem::path icon_cache_directory,
                   IconChangeSink icon_change_sink, ApplySink apply_sink,
                   MenuChangeSink menu_change_sink,
                   LanguageChangeSink language_change_sink,
                   ProgramResolutionLookup resolution_lookup,
                   ProgramResolutionReselect resolution_reselect);

    [[nodiscard]] std::optional<AppSettings> run();
    void create_controls();
    void refresh_localized_text();
    void change_language(UiLanguage language);
    void update_fonts();
    void layout_controls(int width, int height);
    void update_page_visibility();
    void read_windows_hotkey_controls();
    void refresh_windows_hotkey_linkage();
    void refresh_toggle_state_images();
    void refresh_custom_hotkey_list(std::optional<std::size_t> selection = std::nullopt);
    void update_custom_hotkey_buttons();
    [[nodiscard]] std::optional<std::size_t> selected_custom_hotkey_index() const;
    void add_custom_hotkey();
    void edit_selected_custom_hotkey();
    void delete_selected_custom_hotkey();
    void refresh_menu_icon_list(std::optional<std::size_t> selection = std::nullopt);
    [[nodiscard]] std::optional<std::size_t> selected_menu_icon_index() const;
    void update_menu_icon_buttons();
    void choose_selected_menu_icon();
    void restore_selected_menu_icon();
    [[nodiscard]] bool commit_custom_hotkey(
        CustomGlobalHotKey candidate, std::optional<std::size_t> editing_index);
    void begin_capture(std::size_t index);
    void cancel_capture();
    void complete_capture(std::size_t index, const HotKeyGesture& gesture);
    void handle_capture_result(std::size_t index,
                               const KeyboardCaptureResult& result);
    void clear_binding(std::size_t index);
    void update_binding_text(std::size_t index);
    void update_built_in_hotkey_control(std::size_t index);
    [[nodiscard]] BuiltInHotKey& built_in_hotkey(std::size_t index);
    [[nodiscard]] const BuiltInHotKey& built_in_hotkey(std::size_t index) const;
    [[nodiscard]] HotKeyBinding& binding(std::size_t index);
    [[nodiscard]] const HotKeyBinding& binding(std::size_t index) const;
    [[nodiscard]] const wchar_t* field_name(std::size_t index) const noexcept;
    [[nodiscard]] std::wstring windows_hotkey_label(std::size_t index) const;
    [[nodiscard]] const wchar_t* text(SettingsText identifier) const noexcept;
    [[nodiscard]] const wchar_t* text(std::string_view key) const noexcept;
    void diagnose(std::wstring_view message) const noexcept;
    [[nodiscard]] bool apply_current();
    void mark_dirty();
    void update_apply_enabled();
    [[nodiscard]] bool has_unsaved_changes() const;
    [[nodiscard]] bool request_close();
    void create_icon_snapshot();
    [[nodiscard]] bool commit_icon_snapshot();
    void discard_icon_snapshot() noexcept;

    static LRESULT CALLBACK window_procedure(HWND window, UINT message,
                                             WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    HINSTANCE instance_;
    HWND owner_;
    HWND window_ = nullptr;
    AppSettings settings_;
    UiLanguage language_;
    Localization localization_;
    KeyboardManager& keyboard_manager_;
    AvailabilityProbe availability_probe_;
    DiagnosticSink diagnostic_sink_;
    IconChangeSink icon_change_sink_;
    ApplySink apply_sink_;
    MenuChangeSink menu_change_sink_;
    LanguageChangeSink language_change_sink_;
    ProgramResolutionLookup resolution_lookup_;
    ProgramResolutionReselect resolution_reselect_;
    std::vector<MenuIconTarget> menu_icon_targets_;
    std::filesystem::path config_directory_;
    std::filesystem::path icon_cache_directory_;
    std::filesystem::path icon_snapshot_directory_;
    MenuIconCache menu_icon_cache_;
    std::unique_ptr<MenuEditorWindow> menu_editor_;
    AppSettings applied_settings_;
    std::optional<AppSettings> result_;
    std::optional<std::size_t> capturing_;
    HotKeyBinding capture_original_;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HFONT section_font_ = nullptr;
    HFONT title_font_ = nullptr;
    std::array<HWND, 4> labels_{};
    std::array<HWND, 4> capture_buttons_{};
    std::array<HWND, 4> clear_buttons_{};
    std::array<HWND, 4> built_in_hotkey_switches_{};
    std::array<HWND, 4> built_in_hotkey_headers_{};
    std::array<HWND, 26> windows_hotkey_switches_{};
    HWND navigation_ = nullptr;
    HWND title_ = nullptr;
    HWND general_scope_ = nullptr;
    HWND startup_section_ = nullptr;
    HWND appearance_section_ = nullptr;
    HWND hint_ = nullptr;
    HWND status_ = nullptr;
    HWND startup_checkbox_ = nullptr;
    HWND menu_theme_label_ = nullptr;
    HWND menu_theme_combo_ = nullptr;
    HWND language_label_ = nullptr;
    HWND language_combo_ = nullptr;
    HWND windows_hotkey_heading_ = nullptr;
    HWND windows_hotkey_scope_ = nullptr;
    HWND windows_hotkey_runtime_ = nullptr;
    HWND custom_hotkey_heading_ = nullptr;
    HWND custom_hotkey_scope_ = nullptr;
    HWND built_in_hotkeys_section_ = nullptr;
    HWND custom_hotkeys_section_ = nullptr;
    HWND custom_hotkey_list_ = nullptr;
    HWND custom_hotkey_add_button_ = nullptr;
    HWND custom_hotkey_edit_button_ = nullptr;
    HWND custom_hotkey_delete_button_ = nullptr;
    HWND menu_icon_heading_ = nullptr;
    HWND menu_icon_scope_ = nullptr;
    HWND menu_icon_list_ = nullptr;
    HWND menu_icon_select_button_ = nullptr;
    HWND menu_icon_restore_button_ = nullptr;
    HWND menu_editor_heading_ = nullptr;
    HWND menu_editor_scope_ = nullptr;
    HIMAGELIST menu_icon_images_ = nullptr;
    HIMAGELIST custom_hotkey_state_images_ = nullptr;
    HWND save_button_ = nullptr;
    HWND apply_button_ = nullptr;
    HWND cancel_button_ = nullptr;
    bool refreshing_custom_hotkeys_ = false;
    bool icon_dirty_ = false;
    bool icon_snapshot_ready_ = false;
    int selected_page_ = 0;
};

} // namespace simpilot
