#pragma once

#include "simpilot/localization.hpp"
#include "simpilot/menu_model.hpp"

#include <Windows.h>
#include <commctrl.h>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace simpilot {

class MenuEditorWindow final {
public:
    using DiagnosticSink = std::function<void(std::wstring_view)>;
    using DirtySink = std::function<void()>;

    struct ProgramResolutionInfo {
        std::optional<std::filesystem::path> path;
        bool can_reselect = false;
    };

    using ProgramResolutionLookup = std::function<ProgramResolutionInfo(std::wstring_view)>;
    using ProgramResolutionReselect = std::function<std::optional<std::filesystem::path>(
        HWND, std::wstring_view)>;

    struct ElementLocation {
        MenuCategory* parent = nullptr;
        std::size_t index = 0;
        MenuElement* element = nullptr;
    };

    MenuEditorWindow(HINSTANCE instance, HWND parent, std::string language_code,
        std::filesystem::path main_menu_path,
        std::filesystem::path second_menu_path,
        DiagnosticSink diagnostic_sink = {}, DirtySink dirty_sink = {},
        ProgramResolutionLookup resolution_lookup = {},
        ProgramResolutionReselect resolution_reselect = {});
    ~MenuEditorWindow();

    MenuEditorWindow(const MenuEditorWindow&) = delete;
    MenuEditorWindow& operator=(const MenuEditorWindow&) = delete;

    [[nodiscard]] bool create();
    void set_bounds(int x, int y, int width, int height) const;
    void set_visible(bool visible) const;
    void set_language(std::string language_code);
    [[nodiscard]] bool apply();
    [[nodiscard]] bool dirty() const noexcept;

private:
    void load_documents();
    void create_controls();
    void refresh_localized_text();
    void update_fonts();
    void layout_controls(int width, int height);
    void rebuild_tree(MenuElement* selection = nullptr);
    void insert_tree_children(HTREEITEM parent_item, MenuCategory& category);
    void update_selection();
    void update_type_controls();
    void update_resolution_controls();
    void apply_detail_changes();
    void update_tree_item(MenuElement* element);
    void browse_target();
    void reselect_program();
    [[nodiscard]] MenuElement* selected_element() const;
    [[nodiscard]] std::optional<ElementLocation> find_location(MenuElement* element) const;
    [[nodiscard]] std::pair<MenuCategory*, std::size_t> insertion_location() const;
    [[nodiscard]] std::wstring tree_label(const MenuElement& element) const;
    [[nodiscard]] bool has_duplicate_access_key(const MenuElement* element) const;
    [[nodiscard]] MenuElement* first_invalid_element(MenuCategory& category,
                                                     bool root) const;
    void add_item();
    void add_category();
    void add_separator();
    void delete_selected();
    void move_selected(int direction);
    void change_selected_level(int direction);
    void mark_dirty();
    [[nodiscard]] std::optional<std::wstring> read_source(
        const std::filesystem::path& path) const;
    void diagnose(std::wstring_view message) const noexcept;
    [[nodiscard]] const wchar_t* text(std::string_view key) const noexcept;

    static LRESULT CALLBACK window_procedure(HWND window, UINT message,
                                             WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK splitter_procedure(HWND window, UINT message,
        WPARAM wparam, LPARAM lparam, UINT_PTR subclass_identifier,
        DWORD_PTR reference_data);
    static LRESULT CALLBACK tree_procedure(HWND window, UINT message,
        WPARAM wparam, LPARAM lparam, UINT_PTR subclass_identifier,
        DWORD_PTR reference_data);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT draw_tree_item(NMTVCUSTOMDRAW& drawing) const;

    HINSTANCE instance_;
    HWND parent_;
    std::string language_code_;
    Localization localization_;
    std::array<std::filesystem::path, 2> paths_;
    std::array<std::optional<std::wstring>, 2> original_sources_;
    std::array<std::unique_ptr<MenuDocument>, 2> documents_;
    DiagnosticSink diagnostic_sink_;
    DirtySink dirty_sink_;
    ProgramResolutionLookup resolution_lookup_;
    ProgramResolutionReselect resolution_reselect_;
    HWND window_ = nullptr;
    HWND main_menu_button_ = nullptr;
    HWND second_menu_button_ = nullptr;
    HWND tree_ = nullptr;
    HWND splitter_ = nullptr;
    HWND detail_title_ = nullptr;
    HWND name_label_ = nullptr;
    HWND name_edit_ = nullptr;
    HWND access_key_label_ = nullptr;
    HWND access_key_edit_ = nullptr;
    HWND type_label_ = nullptr;
    HWND type_combo_ = nullptr;
    HWND target_label_ = nullptr;
    HWND target_edit_ = nullptr;
    HWND browse_button_ = nullptr;
    HWND resolved_path_label_ = nullptr;
    HWND resolved_path_edit_ = nullptr;
    HWND reselect_program_button_ = nullptr;
    HWND arguments_label_ = nullptr;
    HWND arguments_edit_ = nullptr;
    HWND administrator_checkbox_ = nullptr;
    HWND add_item_button_ = nullptr;
    HWND add_category_button_ = nullptr;
    HWND add_separator_button_ = nullptr;
    HWND delete_button_ = nullptr;
    HWND move_up_button_ = nullptr;
    HWND move_down_button_ = nullptr;
    HWND decrease_level_button_ = nullptr;
    HWND increase_level_button_ = nullptr;
    HFONT font_ = nullptr;
    HFONT section_font_ = nullptr;
    UINT dpi_ = 96;
    int active_menu_ = 0;
    int splitter_x_ = 0;
    bool dirty_ = false;
    bool updating_details_ = false;
};

} // namespace simpilot
