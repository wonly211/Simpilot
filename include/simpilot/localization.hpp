#pragma once

#include <filesystem>
#include <string_view>

namespace simpilot {

enum class UiLanguage {
    english,
    simplified_chinese,
};

enum class UiText {
    app_title,
    menu_two,
    reload_menu,
    settings,
    open_everything,
    everything_unavailable,
    repair_everything,
    repair_everything_success,
    repair_everything_failed,
    reload_failed,
    about,
    maintenance,
    language,
    english,
    simplified_chinese,
    exit,
    configuration_header,
    common,
    notepad,
    calculator,
};

enum class SettingsText {
    title_text, heading_text, main_menu_text, second_menu_text, open_settings_text,
    open_everything_search_text, clear_text, startup_text,
    menu_theme_text, system_theme_text, light_theme_text,
    dark_theme_text, hint_text, capture_text, capture_failed_text,
    escape_cancelled_text, save_text, apply_text, cancel_text, general_tab_text,
    custom_hotkeys_tab_text, system_hotkeys_tab_text, custom_hotkeys_heading_text,
    custom_hotkeys_scope_text, custom_enabled_column_text, custom_hotkey_column_text,
    custom_action_column_text, custom_target_column_text, add_text, edit_text, delete_text,
    open_application_text, open_folder_text, open_file_text, system_hotkeys_heading_text,
    system_hotkeys_scope_text, system_hotkeys_runtime_text, win_l_unsupported_text,
    menu_icons_tab_text, menu_icons_heading_text, menu_icons_scope_text,
    menu_icon_menu_column_text, menu_icon_name_column_text, menu_icon_target_column_text,
    menu_icon_source_column_text, select_icon_text, restore_auto_icon_text,
    automatic_icon_text, custom_icon_text, icon_selection_failed_text,
    quick_launch_tab_text, quick_launch_heading_text, quick_launch_scope_text,
    general_scope_text, startup_section_text, appearance_section_text,
    built_in_hotkeys_section_text, custom_hotkeys_section_text,
    unsaved_changes_text, applied_text,
};

enum class CustomHotKeyText {
    window_title_text, edit_window_title_text, title_text, edit_title_text,
    trigger_heading_text, trigger_type_text,
    allow_modifiers_text, action_heading_text,
    open_application_text, open_folder_text, open_file_text, program_path_text,
    folder_path_text, file_path_text, arguments_text, working_directory_text, browse_text,
    identity_text, normal_identity_text, administrator_identity_text, existing_process_text,
    show_window_text, start_new_text, do_nothing_text, visibility_text,
    normal_visibility_text, minimized_text, maximized_text, hidden_text, save_text,
    cancel_text, modifiers_required_text, win_l_unsupported_text, capture_failed_text,
    invalid_target_text,
};

class Localization final {
public:
    explicit Localization(UiLanguage language);

    [[nodiscard]] static Localization load(const std::filesystem::path& config_directory);
    [[nodiscard]] bool save(const std::filesystem::path& config_directory) const noexcept;

    [[nodiscard]] UiLanguage language() const noexcept;
    void set_language(UiLanguage language) noexcept;
    [[nodiscard]] std::wstring_view text(UiText text) const noexcept;
    [[nodiscard]] std::wstring_view text(SettingsText text) const noexcept;
    [[nodiscard]] std::wstring_view text(CustomHotKeyText text) const noexcept;
    [[nodiscard]] static std::string_view language_code(UiLanguage language) noexcept;

private:
    [[nodiscard]] static UiLanguage detect_windows_language() noexcept;

    UiLanguage language_;
};

} // namespace simpilot
