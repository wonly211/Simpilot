#include "quick_launch_settings_ui.hpp"
#include "settings_visual_style.hpp"
#include "simpilot/atomic_file.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj_core.h>
#include <algorithm>
#include <cwctype>
#include <format>

namespace simpilot {
namespace {

constexpr int menu_icon_list_identifier = 500;
constexpr int menu_icon_select_identifier = 501;
constexpr int menu_icon_restore_identifier = 502;

class UiState final : public QuickLaunchSettingsUi, public std::enable_shared_from_this<UiState> {
    class Page final : public ISettingsPage {
    public:
        Page(std::shared_ptr<UiState> state, bool icons) : state_(std::move(state)), icons_(icons) {}
        ~Page() override { if (window_) DestroyWindow(window_); }
        void create(const SettingsPageContext& context) override {
            WNDCLASSW wc{};
            wc.hInstance = context.instance;
            wc.lpfnWndProc = procedure;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = L"Simpilot.QuickLaunchSettingsPage";
            if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                throw std::runtime_error("Cannot register quick launch page");
            }
            window_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"",
                WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, context.parent, nullptr, context.instance, this);
            if (!window_) throw std::runtime_error("Cannot create quick launch page");
            state_->create_page(icons_, window_, context);
        }
        void layout(RECT bounds, UINT dpi, HFONT font) override {
            MoveWindow(window_, bounds.left, bounds.top, bounds.right - bounds.left,
                       bounds.bottom - bounds.top, TRUE);
            state_->layout_page(icons_, bounds.right - bounds.left, bounds.bottom - bounds.top, dpi, font);
        }
        void show(bool visible) override {
            ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
            if (!icons_ && state_->menu_editor_) state_->menu_editor_->set_visible(visible);
        }
        void refresh_language(const Localization& localization) override { state_->refresh_language(localization); }
    private:
        static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
            if (message == WM_NCCREATE) SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
            auto* page = reinterpret_cast<Page*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (!page) return DefWindowProcW(window, message, wparam, lparam);
            try {
                page->state_->window_ = window;
                if (message == WM_NCDESTROY) page->window_ = nullptr;
                if (message == WM_ERASEBKGND) return settings_visual_style::erase_background(window, wparam);
                if (settings_visual_style::is_color_message(message))
                    return settings_visual_style::handle_color_message(message, wparam, lparam);
                if (message == WM_COMMAND) {
                    if (LOWORD(wparam) == menu_icon_select_identifier) page->state_->choose_selected_menu_icon();
                    if (LOWORD(wparam) == menu_icon_restore_identifier) page->state_->restore_selected_menu_icon();
                    if (LOWORD(wparam) == 11 && HIWORD(wparam) == CBN_SELCHANGE) {
                        page->state_->draft_.menu_theme = static_cast<MenuTheme>(
                            SendMessageW(page->state_->theme_, CB_GETCURSEL, 0, 0));
                        page->state_->mark_dirty();
                    }
                    if (LOWORD(wparam) == 12 && HIWORD(wparam) == CBN_SELCHANGE) {
                        page->state_->draft_.menu_size = static_cast<MenuSize>(
                            SendMessageW(page->state_->size_, CB_GETCURSEL, 0, 0));
                        page->state_->mark_dirty();
                    }
                }
                if (message == WM_NOTIFY) {
                    const auto* notification = reinterpret_cast<NMHDR*>(lparam);
                    if (notification->hwndFrom == page->state_->menu_icon_list_) {
                        if (notification->code == LVN_ITEMCHANGED) page->state_->update_menu_icon_buttons();
                        if (notification->code == NM_DBLCLK) page->state_->choose_selected_menu_icon();
                    }
                }
                return DefWindowProcW(window, message, wparam, lparam);
            } catch (...) {
                page->state_->diagnose(L"quick launch settings callback failed");
                return message == WM_CREATE ? -1 : 0;
            }
        }
        std::shared_ptr<UiState> state_;
        bool icons_;
        HWND window_ = nullptr;
    };
public:
    UiState(QuickLaunchSettings& draft, std::filesystem::path directory,
        std::filesystem::path icons, std::function<std::vector<MenuIconTarget>()> targets,
        MenuEditorWindow::ProgramResolutionLookup lookup,
        MenuEditorWindow::ProgramResolutionReselect reselect,
        std::function<void(std::wstring_view)> diagnose)
        : draft_(draft), config_directory_(std::move(directory)), icon_cache_directory_(std::move(icons)),
          icon_snapshot_directory_(icon_cache_directory_.parent_path()
              / std::format(L"SettingsDraft-{}-{}", GetCurrentProcessId(), GetTickCount64())),
          menu_icon_cache_(icon_snapshot_directory_), targets_(std::move(targets)),
          resolution_lookup_(std::move(lookup)), resolution_reselect_(std::move(reselect)),
          diagnostic_sink_(std::move(diagnose)) { create_icon_snapshot(); }
    ~UiState() override {
        menu_editor_.reset();
        if (menu_icon_images_) ImageList_Destroy(menu_icon_images_);
        discard_icon_snapshot();
        if (!retain_backup_) cleanup_backup();
    }
    std::unique_ptr<ISettingsPage> page(bool icons) override {
        return std::make_unique<Page>(shared_from_this(), icons);
    }
    bool dirty() const override { return icon_dirty_ || (menu_editor_ && menu_editor_->dirty()); }
    bool validate() override { return !menu_editor_ || menu_editor_->validate_draft(); }
    bool prepare() override {
        if (retain_backup_) return false;
        if (!validate()) return false;
        if (menu_editor_ && !menu_editor_->prepare_apply()) return false;
        cleanup_backup();
        menu_changed_ = menu_editor_ && menu_editor_->dirty();
        applied_ = false;
        if (menu_changed_ && !menu_editor_->capture_source_snapshot(menu_snapshot_)) return false;
        if (icon_dirty_ && !icon_snapshot_ready_) return false;
        if (!menu_changed_ && !icon_dirty_) return true;
        backup_ = icon_snapshot_directory_.parent_path()
            / std::format(L"SettingsRecovery-{}-{}", GetCurrentProcessId(), GetTickCount64());
        std::filesystem::create_directories(backup_);
        if (menu_changed_) {
            for (int i = 0; i < 2; ++i) {
                const auto name = i == 0 ? L"Simpilot.ini" : L"Simpilot2.ini";
                if (menu_snapshot_[i]) std::filesystem::copy_file(config_directory_ / name, backup_ / name);
            }
        }
        if (icon_dirty_) {
            std::filesystem::create_directories(backup_ / L"icons");
            copy_icons(icon_cache_directory_, backup_ / L"icons", false);
        }
        return true;
    }
    bool apply() override {
        applied_ = true;
        if (menu_editor_ && !menu_editor_->apply()) return false;
        if (icon_dirty_) copy_icons(icon_snapshot_directory_, icon_cache_directory_, true);
        return true;
    }
    bool rollback() override {
        if (menu_editor_) menu_editor_->discard_prepared();
        if (!applied_) { cleanup_backup(); return true; }
        bool restored = !menu_changed_ || menu_editor_->restore_source_snapshot(menu_snapshot_);
        try {
            if (menu_changed_) {
                for (const auto* name : {L"Simpilot.ini", L"Simpilot2.ini"}) {
                    if (!std::filesystem::exists(backup_ / name)) continue;
                    AtomicFileReplacement replacement(config_directory_ / name);
                    std::filesystem::copy_file(backup_ / name, replacement.temporary_path(),
                        std::filesystem::copy_options::overwrite_existing);
                    if (!replacement.commit()) restored = false;
                }
            }
            if (icon_dirty_) copy_icons(backup_ / L"icons", icon_cache_directory_, true);
        } catch (...) { restored = false; }
        retain_backup_ = !restored;
        if (restored) cleanup_backup();
        else diagnose(L"quick launch rollback failed; backup retained at " + backup_.wstring());
        applied_ = false;
        return restored;
    }
    void finish() override {
        cleanup_backup();
        icon_dirty_ = false;
        applied_ = false;
        create_icon_snapshot();
        menu_icon_targets_ = targets_();
        refresh_menu_icon_list();
    }
private:
    static std::vector<std::filesystem::path> icons_in(const std::filesystem::path& path) {
        std::vector<std::filesystem::path> result;
        if (!std::filesystem::exists(path)) return result;
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_regular_file() && entry.path().filename().wstring().ends_with(L".custom.ico"))
                result.push_back(entry.path());
        }
        return result;
    }
    static void copy_icons(const std::filesystem::path& from, const std::filesystem::path& to, bool replace) {
        const auto sources = icons_in(from);
        std::filesystem::create_directories(to);
        if (replace) for (const auto& file : icons_in(to)) std::filesystem::remove(file);
        for (const auto& file : sources) std::filesystem::copy_file(file, to / file.filename(),
            std::filesystem::copy_options::overwrite_existing);
    }
    void cleanup_backup() noexcept {
        if (backup_.empty() || retain_backup_) return;
        std::error_code error;
        std::filesystem::remove_all(backup_, error);
        if (!error) backup_.clear();
    }
    void create_page(bool icons, HWND parent, const SettingsPageContext& context);
    void layout_page(bool icons, int width, int height, UINT dpi, HFONT font);
    void refresh_language(const Localization& localization);
    void refresh_menu_icon_list(std::optional<std::size_t> selection = {});
    std::optional<std::size_t> selected_menu_icon_index() const;
    void update_menu_icon_buttons();
    void choose_selected_menu_icon();
    void restore_selected_menu_icon();
    void create_icon_snapshot();
    void discard_icon_snapshot() noexcept;
    void mark_dirty() { if (changed_) changed_(); }
    void diagnose(std::wstring_view message) const noexcept {
        try { if (diagnostic_sink_) diagnostic_sink_(message); } catch (...) {}
    }
    const wchar_t* text(std::string_view key) const { return localization_.text(key).data(); }
    QuickLaunchSettings& draft_;
    std::filesystem::path config_directory_, icon_cache_directory_, icon_snapshot_directory_, backup_;
    MenuIconCache menu_icon_cache_;
    std::function<std::vector<MenuIconTarget>()> targets_;
    MenuEditorWindow::ProgramResolutionLookup resolution_lookup_;
    MenuEditorWindow::ProgramResolutionReselect resolution_reselect_;
    std::function<void(std::wstring_view)> diagnostic_sink_;
    std::function<void()> changed_;
    Localization localization_{"zh-CN"};
    settings_visual_style::PageTypography editor_typography_, icon_typography_;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr, editor_heading_ = nullptr,
         theme_label_ = nullptr, theme_ = nullptr, size_label_ = nullptr, size_ = nullptr;
    HWND menu_icon_heading_ = nullptr, menu_icon_list_ = nullptr,
         menu_icon_select_button_ = nullptr, menu_icon_restore_button_ = nullptr;
    HIMAGELIST menu_icon_images_ = nullptr;
    UINT dpi_ = 96;
    UINT icon_dpi_ = 0;
    std::vector<MenuIconTarget> menu_icon_targets_;
    std::unique_ptr<MenuEditorWindow> menu_editor_;
    MenuEditorWindow::SourceSnapshot menu_snapshot_;
    bool icon_dirty_ = false, icon_snapshot_ready_ = false, menu_changed_ = false,
         applied_ = false, retain_backup_ = false;
};

void UiState::create_page(bool icons, HWND parent, const SettingsPageContext& context) {
    instance_ = context.instance;
    window_ = parent;
    dpi_ = context.dpi;
    changed_ = context.changed;
    localization_ = context.localization;
    if (icons) {
        menu_icon_targets_ = targets_();
            menu_icon_heading_ = CreateWindowW(L"STATIC", text("settings.menu_icons.heading"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    menu_icon_list_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(menu_icon_list_identifier)), instance_, nullptr);
    ListView_SetExtendedListViewStyle(menu_icon_list_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    settings_visual_style::style_list_view(menu_icon_list_);
    menu_icon_images_ = ImageList_Create(MulDiv(32, dpi_, 96), MulDiv(36, dpi_, 96),
        ILC_COLOR32 | ILC_MASK, 8, 8);
    icon_dpi_ = dpi_;
    ListView_SetImageList(menu_icon_list_, menu_icon_images_, LVSIL_SMALL);
    LVCOLUMNW icon_column{.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM};
    icon_column.cx = 90;
    icon_column.pszText = const_cast<wchar_t*>(text("settings.menu_icons.column.menu"));
    ListView_InsertColumn(menu_icon_list_, 0, &icon_column);
    icon_column.iSubItem = 1;
    icon_column.cx = 180;
    icon_column.pszText = const_cast<wchar_t*>(text("settings.menu_icons.column.name"));
    ListView_InsertColumn(menu_icon_list_, 1, &icon_column);
    icon_column.iSubItem = 2;
    icon_column.cx = 400;
    icon_column.pszText = const_cast<wchar_t*>(text("settings.menu_icons.column.target"));
    ListView_InsertColumn(menu_icon_list_, 2, &icon_column);
    icon_column.iSubItem = 3;
    icon_column.cx = 100;
    icon_column.pszText = const_cast<wchar_t*>(text("settings.menu_icons.column.source"));
    ListView_InsertColumn(menu_icon_list_, 3, &icon_column);
    menu_icon_select_button_ = CreateWindowW(L"BUTTON", text("settings.menu_icons.select"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(menu_icon_select_identifier)),
        instance_, nullptr);
    menu_icon_restore_button_ = CreateWindowW(L"BUTTON", text("settings.menu_icons.restore_auto"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(menu_icon_restore_identifier)),
        instance_, nullptr);
    settings_visual_style::style_button(menu_icon_select_button_);
    settings_visual_style::style_button(menu_icon_restore_button_);
    refresh_menu_icon_list();


    } else {
        editor_heading_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, parent, nullptr, instance_, nullptr);
        theme_label_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, parent, nullptr, instance_, nullptr);
        theme_ = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            0, 0, 0, 0, parent, reinterpret_cast<HMENU>(11), instance_, nullptr);
        size_label_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, parent, nullptr, instance_, nullptr);
        size_ = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            0, 0, 0, 0, parent, reinterpret_cast<HMENU>(12), instance_, nullptr);
        menu_editor_ = std::make_unique<MenuEditorWindow>(instance_, parent,
            std::string(localization_.language_code()), config_directory_ / L"Simpilot.ini",
            config_directory_ / L"Simpilot2.ini", diagnostic_sink_,
            [this] { mark_dirty(); }, resolution_lookup_, resolution_reselect_);
        if (!menu_editor_->create()) throw std::runtime_error("Cannot create menu editor");
    }
    refresh_language(context.localization);
}
void UiState::layout_page(bool icons, int width, int height, UINT dpi, HFONT font) {
    dpi_ = dpi;
    auto& typography = icons ? icon_typography_ : editor_typography_;
    typography.update(font, dpi);
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    const auto place = [&](HWND control, int x, int y, int w, int h) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(typography.body()), TRUE);
        MoveWindow(control, x, y, std::max(1, w), std::max(1, h), TRUE);
    };
    if (!icons) {
        menu_editor_->set_dpi(dpi);
        place(editor_heading_, 0, 0, width, scale(36));
        SendMessageW(editor_heading_, WM_SETFONT, reinterpret_cast<WPARAM>(typography.title()), TRUE);
        place(theme_label_, 0, scale(57), scale(120), scale(24));
        SendMessageW(theme_label_, WM_SETFONT, reinterpret_cast<WPARAM>(typography.section()), TRUE);
        const int theme_x = scale(136);
        place(theme_, theme_x, scale(52), std::min(scale(200), width - theme_x), scale(180));
        place(size_label_, 0, scale(101), scale(190), scale(24));
        SendMessageW(size_label_, WM_SETFONT, reinterpret_cast<WPARAM>(typography.section()), TRUE);
        place(size_, scale(206), scale(96), std::min(scale(200), width - scale(206)), scale(180));
        menu_editor_->set_bounds(0, scale(144), width, std::max(1, height - scale(144)));
        return;
    }
    place(menu_icon_heading_, 0, 0, width, scale(36));
    SendMessageW(menu_icon_heading_, WM_SETFONT, reinterpret_cast<WPARAM>(typography.title()), TRUE);
    place(menu_icon_select_button_, 0, scale(56), scale(120), scale(32));
    place(menu_icon_restore_button_, scale(128), scale(56), scale(180), scale(32));
    place(menu_icon_list_, 0, scale(100), width, height - scale(100));
    if (icon_dpi_ != dpi || !menu_icon_images_) {
        if (const auto next = ImageList_Create(scale(32), scale(36), ILC_COLOR32 | ILC_MASK, 8, 8)) {
            const auto selection = selected_menu_icon_index();
            ListView_SetImageList(menu_icon_list_, next, LVSIL_SMALL);
            if (menu_icon_images_) ImageList_Destroy(menu_icon_images_);
            menu_icon_images_ = next;
            icon_dpi_ = dpi;
            refresh_menu_icon_list(selection);
        }
    }
    RECT list_client{};
    GetClientRect(menu_icon_list_, &list_client);
    const int available = std::max(1L, list_client.right - scale(4));
    const int menu_width = scale(96), source_width = scale(120);
    const int name_width = scale(176);
    const int widths[]{menu_width, name_width,
        std::max(scale(240), available - menu_width - name_width - source_width), source_width};
    for (int i = 0; i < 4; ++i) ListView_SetColumnWidth(menu_icon_list_, i, widths[i]);
}
void UiState::refresh_language(const Localization& localization) {
    localization_ = localization;
    if (menu_editor_) {
        SetWindowTextW(editor_heading_, text("settings.quick_launch.heading"));
        SetWindowTextW(theme_label_, text("settings.menu_theme"));
        SendMessageW(theme_, CB_RESETCONTENT, 0, 0);
        for (auto key : {"settings.system_theme", "settings.light_theme", "settings.dark_theme"})
            SendMessageW(theme_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text(key)));
        SendMessageW(theme_, CB_SETCURSEL, static_cast<WPARAM>(draft_.menu_theme), 0);
        SetWindowTextW(size_label_, text("settings.menu_size"));
        SendMessageW(size_, CB_RESETCONTENT, 0, 0);
        for (auto key : {"settings.menu_size.small", "settings.menu_size.medium", "settings.menu_size.large"})
            SendMessageW(size_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text(key)));
        SendMessageW(size_, CB_SETCURSEL, static_cast<WPARAM>(draft_.menu_size), 0);
        menu_editor_->set_language(std::string(localization_.language_code()));
    }
    if (!menu_icon_list_) return;
    settings_visual_style::set_list_empty_text(menu_icon_list_, text("settings.menu_icons.empty"));
    SetWindowTextW(menu_icon_heading_, text("settings.menu_icons.heading"));
    SetWindowTextW(menu_icon_select_button_, text("settings.menu_icons.select"));
    SetWindowTextW(menu_icon_restore_button_, text("settings.menu_icons.restore_auto"));
    const std::string_view keys[]{"settings.menu_icons.column.menu", "settings.menu_icons.column.name",
                             "settings.menu_icons.column.target", "settings.menu_icons.column.source"};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW column{.mask = LVCF_TEXT, .pszText = const_cast<wchar_t*>(text(keys[i]))};
        ListView_SetColumn(menu_icon_list_, i, &column);
    }
    menu_icon_targets_ = targets_();
    refresh_menu_icon_list();
}
void UiState::refresh_menu_icon_list(
    const std::optional<std::size_t> selection) {
    if (!menu_icon_list_) return;
    ListView_DeleteAllItems(menu_icon_list_);
    if (menu_icon_images_) ImageList_RemoveAll(menu_icon_images_);
    for (std::size_t index = 0; index < menu_icon_targets_.size(); ++index) {
        const auto& target = menu_icon_targets_[index];
        const auto icon = menu_icon_cache_.icon_for_customization(
            target.custom_key, target.icon_source, target.kind);
        const auto image = icon && menu_icon_images_
            ? ImageList_AddIcon(menu_icon_images_, icon) : I_IMAGENONE;
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM | (image == I_IMAGENONE ? 0U : LVIF_IMAGE);
        item.iItem = static_cast<int>(index);
        item.pszText = const_cast<wchar_t*>(target.menu_name.c_str());
        item.iImage = image;
        item.lParam = static_cast<LPARAM>(index);
        const auto row = ListView_InsertItem(menu_icon_list_, &item);
        ListView_SetItemText(menu_icon_list_, row, 1,
            const_cast<wchar_t*>(target.display_name.c_str()));
        ListView_SetItemText(menu_icon_list_, row, 2,
            const_cast<wchar_t*>(target.target.c_str()));
        ListView_SetItemText(menu_icon_list_, row, 3,
            const_cast<wchar_t*>(text(menu_icon_cache_.has_custom_icon(target.custom_key)
                ? "settings.menu_icons.custom" : "settings.menu_icons.automatic")));
        if (selection && *selection == index) {
            ListView_SetItemState(menu_icon_list_, row,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(menu_icon_list_, row, FALSE);
        }
    }
    update_menu_icon_buttons();
}

std::optional<std::size_t> UiState::selected_menu_icon_index() const {
    const auto row = ListView_GetNextItem(menu_icon_list_, -1, LVNI_SELECTED);
    if (row < 0) return std::nullopt;
    LVITEMW item{.mask = LVIF_PARAM, .iItem = row};
    if (!ListView_GetItem(menu_icon_list_, &item)) return std::nullopt;
    const auto index = static_cast<std::size_t>(item.lParam);
    return index < menu_icon_targets_.size()
        ? std::optional<std::size_t>(index) : std::nullopt;
}

void UiState::update_menu_icon_buttons() {
    const auto selection = selected_menu_icon_index();
    EnableWindow(menu_icon_select_button_, selection && icon_snapshot_ready_ ? TRUE : FALSE);
    EnableWindow(menu_icon_restore_button_, icon_snapshot_ready_ && selection
        && menu_icon_cache_.has_custom_icon(menu_icon_targets_[*selection].custom_key)
            ? TRUE : FALSE);
}

void UiState::choose_selected_menu_icon() {
    if (!icon_snapshot_ready_) return;
    const auto selection = selected_menu_icon_index();
    if (!selection) return;

    std::array<wchar_t, 32768> source{};
    std::wstring filter;
    const std::array<std::pair<std::string_view, std::wstring_view>, 4> filters{{
        {"settings.menu_icons.filter.supported", L"*.ico;*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.exe;*.dll"},
        {"settings.menu_icons.filter.images", L"*.ico;*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff"},
        {"settings.menu_icons.filter.resources", L"*.exe;*.dll"},
        {"settings.menu_icons.filter.all", L"*.*"}
    }};
    for (const auto& [key, pattern] : filters) {
        filter.append(text(key));
        filter.push_back(L'\0');
        filter.append(pattern);
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = source.data();
    dialog.nMaxFile = static_cast<DWORD>(source.size());
    dialog.lpstrTitle = text("settings.menu_icons.select");
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog) || source.front() == L'\0') return;

    int source_index = 0;
    auto extension = std::filesystem::path(source.data()).extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    if (extension == L".exe" || extension == L".dll") {
        if (PickIconDlg(window_, source.data(), static_cast<UINT>(source.size()),
                        &source_index) != 1 || source.front() == L'\0') return;
    }
    if (!menu_icon_cache_.set_custom_icon(menu_icon_targets_[*selection].custom_key,
                                          source.data(), source_index)) {
        MessageBoxW(window_, text("settings.menu_icons.selection_failed"), text("settings.title"),
                    MB_OK | MB_ICONWARNING);
        return;
    }
    icon_dirty_ = true;
    mark_dirty();
    refresh_menu_icon_list(*selection);
}

void UiState::restore_selected_menu_icon() {
    const auto selection = selected_menu_icon_index();
    if (!selection || !menu_icon_cache_.remove_custom_icon(
            menu_icon_targets_[*selection].custom_key)) {
        return;
    }
    icon_dirty_ = true;
    mark_dirty();
    refresh_menu_icon_list(*selection);
}

void UiState::create_icon_snapshot() {
    icon_snapshot_ready_ = false;
    std::error_code error;
    std::filesystem::remove_all(icon_snapshot_directory_, error);
    error.clear();
    std::filesystem::create_directories(icon_snapshot_directory_, error);
    if (error) {
        diagnose(L"menu icon draft directory creation failed");
        return;
    }
    if (!std::filesystem::exists(icon_cache_directory_, error)) {
        icon_snapshot_ready_ = !error;
        return;
    }
    if (!std::filesystem::is_directory(icon_cache_directory_, error)) {
        diagnose(L"menu icon cache path is not a directory");
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(icon_cache_directory_, error)) {
        if (error) break;
        if (!entry.is_regular_file(error)
            || !entry.path().filename().wstring().ends_with(L".custom.ico")) {
            error.clear();
            continue;
        }
        std::filesystem::copy_file(entry.path(),
            icon_snapshot_directory_ / entry.path().filename(),
            std::filesystem::copy_options::overwrite_existing, error);
        if (error) break;
    }
    if (error) {
        diagnose(L"menu icon snapshot creation failed");
        std::filesystem::remove_all(icon_snapshot_directory_, error);
    } else {
        icon_snapshot_ready_ = true;
    }
    menu_icon_cache_.clear();
}

void UiState::discard_icon_snapshot() noexcept {
    if (icon_snapshot_directory_.empty()) return;
    std::error_code error;
    std::filesystem::remove_all(icon_snapshot_directory_, error);
    icon_snapshot_ready_ = false;
}


} // namespace
std::shared_ptr<QuickLaunchSettingsUi> make_quick_launch_settings_ui(
    QuickLaunchSettings& draft, const std::filesystem::path& directory,
    const std::filesystem::path& icons, std::function<std::vector<MenuIconTarget>()> targets,
    MenuEditorWindow::ProgramResolutionLookup lookup,
    MenuEditorWindow::ProgramResolutionReselect reselect,
    std::function<void(std::wstring_view)> diagnose) {
    return std::make_shared<UiState>(draft, directory, icons, std::move(targets),
        std::move(lookup), std::move(reselect), std::move(diagnose));
}
} // namespace simpilot
