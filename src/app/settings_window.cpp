#include "settings_window.hpp"

#include "custom_hotkey_dialog.hpp"
#include "hotkey_capture_button.hpp"
#include "resource.h"
#include "settings_visual_style.hpp"
#include "toggle_switch.hpp"

#include <commctrl.h>
#include <shlobj_core.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <format>
#include <iterator>
#include <string>

namespace simpilot {
namespace {

constexpr auto settings_class_name = L"Simpilot.SettingsWindow";
constexpr int save_identifier = 1;
constexpr int cancel_identifier = 2;
constexpr int apply_identifier = 3;
constexpr int startup_identifier = 10;
constexpr int menu_theme_identifier = 11;
constexpr int built_in_hotkey_switch_base_identifier = 20;
constexpr int edit_base_identifier = 100;
constexpr int clear_base_identifier = 200;
constexpr int navigation_identifier = 250;
constexpr int windows_hotkey_base_identifier = 300;
constexpr int custom_hotkey_list_identifier = 400;
constexpr int custom_hotkey_add_identifier = 401;
constexpr int custom_hotkey_delete_identifier = 402;
constexpr int custom_hotkey_edit_identifier = 403;
constexpr int menu_icon_list_identifier = 500;
constexpr int menu_icon_select_identifier = 501;
constexpr int menu_icon_restore_identifier = 502;
constexpr std::size_t unsupported_windows_hotkey_index = static_cast<std::size_t>(L'L' - L'A');
constexpr std::size_t visible_windows_hotkey_count = 25;
using enum SettingsText;

constexpr std::size_t windows_hotkey_index_from_visual(
    const std::size_t visual_index) noexcept {
    return visual_index < unsupported_windows_hotkey_index
        ? visual_index : visual_index + 1;
}

void set_font(const HWND control, const HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring windows_error_message(const DWORD error) {
    wchar_t* buffer = nullptr;
    const auto length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = length != 0 && buffer
        ? std::wstring(buffer, length) : L"Unknown Windows error";
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) {
        result.pop_back();
    }
    return result;
}

} // namespace

std::optional<AppSettings> SettingsWindow::show_modal(
    const HINSTANCE instance, const HWND owner, const AppSettings& current,
    const UiLanguage language, KeyboardManager& keyboard_manager,
    AvailabilityProbe availability_probe,
    DiagnosticSink diagnostic_sink, std::vector<MenuIconTarget> menu_icon_targets,
    std::filesystem::path config_directory, std::filesystem::path icon_cache_directory,
    IconChangeSink icon_change_sink,
    ApplySink apply_sink, MenuChangeSink menu_change_sink) {
    SettingsWindow window(instance, owner, current, language, keyboard_manager,
                          std::move(availability_probe), std::move(diagnostic_sink),
                          std::move(menu_icon_targets), std::move(config_directory),
                          std::move(icon_cache_directory),
                          std::move(icon_change_sink), std::move(apply_sink),
                          std::move(menu_change_sink));
    return window.run();
}

SettingsWindow::SettingsWindow(const HINSTANCE instance, const HWND owner,
                               AppSettings current, const UiLanguage language,
                               KeyboardManager& keyboard_manager,
                               AvailabilityProbe availability_probe,
                               DiagnosticSink diagnostic_sink,
                               std::vector<MenuIconTarget> menu_icon_targets,
                               std::filesystem::path config_directory,
                               std::filesystem::path icon_cache_directory,
                               IconChangeSink icon_change_sink, ApplySink apply_sink,
                               MenuChangeSink menu_change_sink)
    : instance_(instance), owner_(owner), settings_(std::move(current)), language_(language),
      keyboard_manager_(keyboard_manager),
      availability_probe_(std::move(availability_probe)),
      diagnostic_sink_(std::move(diagnostic_sink)),
      icon_change_sink_(std::move(icon_change_sink)),
      apply_sink_(std::move(apply_sink)),
      menu_change_sink_(std::move(menu_change_sink)),
      menu_icon_targets_(std::move(menu_icon_targets)),
      config_directory_(std::move(config_directory)),
      icon_cache_directory_(std::move(icon_cache_directory)),
      icon_snapshot_directory_(icon_cache_directory_.parent_path()
          / std::format(L"SettingsDraft-{}", GetCurrentProcessId())),
      menu_icon_cache_(icon_snapshot_directory_), applied_settings_(settings_) {}

std::optional<AppSettings> SettingsWindow::run() {
    INITCOMMONCONTROLSEX common_controls{
        .dwSize = sizeof(common_controls),
        .dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES,
    };
    InitCommonControlsEx(&common_controls);
    WNDCLASSW window_class{
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &SettingsWindow::window_procedure,
        .hInstance = instance_,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = settings_class_name,
    };
    RegisterClassW(&window_class);

    create_icon_snapshot();
    const auto system_dpi = GetDpiForSystem();
    const auto monitor = MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_information{.cbSize = sizeof(monitor_information)};
    GetMonitorInfoW(monitor, &monitor_information);
    const auto work_width = monitor_information.rcWork.right - monitor_information.rcWork.left;
    const auto work_height = monitor_information.rcWork.bottom - monitor_information.rcWork.top;
    const auto requested_width = std::min(MulDiv(1080, system_dpi, 96),
                                          static_cast<int>(work_width * 92 / 100));
    const auto requested_height = std::min(MulDiv(760, system_dpi, 96),
                                           static_cast<int>(work_height * 92 / 100));
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, settings_class_name, text(title_text),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, requested_width,
        requested_height, owner_, nullptr, instance_, this);
    if (!window_) return std::nullopt;
    settings_visual_style::apply_application_icons(window_, instance_, IDI_SIMPILOT);
    RECT window_rectangle{};
    GetWindowRect(window_, &window_rectangle);
    const auto width = window_rectangle.right - window_rectangle.left;
    const auto height = window_rectangle.bottom - window_rectangle.top;
    const auto x = monitor_information.rcWork.left
        + (monitor_information.rcWork.right - monitor_information.rcWork.left - width) / 2;
    const auto y = monitor_information.rcWork.top
        + (monitor_information.rcWork.bottom - monitor_information.rcWork.top - height) / 2;
    SetWindowPos(window_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    MSG message{};
    while (IsWindow(window_) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN
            && (GetKeyState(VK_CONTROL) & 0x8000) != 0
            && (message.wParam == L'S' || message.wParam == VK_TAB)) {
            if (message.wParam == L'S') {
                (void)apply_current();
            } else {
                const auto direction = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1;
                selected_page_ = (selected_page_ + direction + 5) % 5;
                SendMessageW(navigation_, LB_SETCURSEL, selected_page_, 0);
                update_page_visibility();
                SetFocus(navigation_);
            }
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            wchar_t class_name[16]{};
            GetClassNameW(message.hwnd, class_name, static_cast<int>(std::size(class_name)));
            if (_wcsicmp(class_name, L"Edit") == 0) continue;
        }
        if (!IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (font_) DeleteObject(font_);
    if (section_font_) DeleteObject(section_font_);
    if (title_font_) DeleteObject(title_font_);
    font_ = nullptr;
    section_font_ = nullptr;
    title_font_ = nullptr;
    discard_icon_snapshot();
    return result_;
}

void SettingsWindow::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    navigation_ = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED
            | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(navigation_identifier)), instance_, nullptr);
    for (const auto identifier : {general_tab_text, quick_launch_tab_text,
                                  menu_icons_tab_text, custom_hotkeys_tab_text,
                                  system_hotkeys_tab_text}) {
        SendMessageW(navigation_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text(identifier)));
    }
    SendMessageW(navigation_, LB_SETCURSEL, 0, 0);
    SendMessageW(navigation_, LB_SETITEMHEIGHT, 0, MulDiv(42, dpi_, 96));

    title_ = CreateWindowW(L"STATIC", text(heading_text), WS_CHILD | WS_VISIBLE,
                           0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    general_scope_ = CreateWindowW(L"STATIC", text(general_scope_text),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    startup_section_ = CreateWindowW(L"STATIC", text(startup_section_text),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    appearance_section_ = CreateWindowW(L"STATIC", text(appearance_section_text),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    for (std::size_t index = 0; index < capture_buttons_.size(); ++index) {
        labels_[index] = CreateWindowW(L"STATIC", field_name(index), WS_CHILD | WS_VISIBLE,
                                       0, 0, 0, 0, window_, nullptr, instance_, nullptr);
        capture_buttons_[index] = CreateWindowW(L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(edit_base_identifier + index)),
            instance_, nullptr);
        clear_buttons_[index] = CreateWindowW(L"BUTTON", text(clear_text),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(clear_base_identifier + index)),
            instance_, nullptr);
        const auto accessible_name = language_ == UiLanguage::simplified_chinese
            ? std::format(L"启用{}热键", field_name(index))
            : std::format(L"Enable {} hotkey", field_name(index));
        built_in_hotkey_switches_[index] = toggle_switch::create(
            instance_, window_, built_in_hotkey_switch_base_identifier
                + static_cast<int>(index), accessible_name,
            built_in_hotkey(index).enabled);
        update_built_in_hotkey_control(index);
    }
    startup_checkbox_ = CreateWindowW(L"BUTTON", text(startup_text),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(startup_identifier)), instance_, nullptr);
    SendMessageW(startup_checkbox_, BM_SETCHECK,
                 settings_.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
    menu_theme_label_ = CreateWindowW(L"STATIC", text(menu_theme_text),
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    menu_theme_combo_ = CreateWindowW(WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(menu_theme_identifier)), instance_, nullptr);
    SendMessageW(menu_theme_combo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(text(system_theme_text)));
    SendMessageW(menu_theme_combo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(text(light_theme_text)));
    SendMessageW(menu_theme_combo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(text(dark_theme_text)));
    SendMessageW(menu_theme_combo_, CB_SETMINVISIBLE, 3, 0);
    SendMessageW(menu_theme_combo_, CB_SETCURSEL,
                 static_cast<WPARAM>(settings_.menu_theme), 0);
    hint_ = CreateWindowW(L"STATIC", text(hint_text), WS_CHILD | WS_VISIBLE,
                          0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    status_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                            0, 0, 0, 0, window_, nullptr, instance_, nullptr);

    windows_hotkey_heading_ = CreateWindowW(L"STATIC", text(system_hotkeys_heading_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    windows_hotkey_scope_ = CreateWindowW(L"STATIC", text(system_hotkeys_scope_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    windows_hotkey_runtime_ = CreateWindowW(L"STATIC", text(system_hotkeys_runtime_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    for (std::size_t visual_index = 0; visual_index < visible_windows_hotkey_count;
         ++visual_index) {
        const auto index = windows_hotkey_index_from_visual(visual_index);
        const auto label = windows_hotkey_label(index);
        windows_hotkey_switches_[index] = toggle_switch::create(
            instance_, window_, windows_hotkey_base_identifier + static_cast<int>(index),
            label, settings_.disabled_windows_hotkeys[index], true);
    }

    custom_hotkey_heading_ = CreateWindowW(L"STATIC", text(custom_hotkeys_heading_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    custom_hotkey_scope_ = CreateWindowW(L"STATIC", text(custom_hotkeys_scope_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    built_in_hotkeys_section_ = CreateWindowW(L"STATIC", text(built_in_hotkeys_section_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    const std::array built_in_headers{
        language_ == UiLanguage::simplified_chinese ? L"功能" : L"Function",
        text(custom_hotkey_column_text),
        language_ == UiLanguage::simplified_chinese ? L"操作" : L"Action",
        text(custom_enabled_column_text),
    };
    for (std::size_t index = 0; index < built_in_hotkey_headers_.size(); ++index) {
        built_in_hotkey_headers_[index] = CreateWindowW(
            L"STATIC", built_in_headers[index], WS_CHILD,
            0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    }
    custom_hotkeys_section_ = CreateWindowW(L"STATIC", text(custom_hotkeys_section_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    custom_hotkey_list_ = CreateWindowExW(WS_EX_STATICEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS
            | LVS_SHAREIMAGELISTS,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(custom_hotkey_list_identifier)), instance_, nullptr);
    ListView_SetExtendedListViewStyle(custom_hotkey_list_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES | LVS_EX_LABELTIP);
    refresh_toggle_state_images();
    settings_visual_style::style_list_view(custom_hotkey_list_);
    LVCOLUMNW column{.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM};
    column.cx = 80;
    column.pszText = const_cast<wchar_t*>(text(custom_enabled_column_text));
    ListView_InsertColumn(custom_hotkey_list_, 0, &column);
    column.iSubItem = 1;
    column.cx = 180;
    column.pszText = const_cast<wchar_t*>(text(custom_hotkey_column_text));
    ListView_InsertColumn(custom_hotkey_list_, 1, &column);
    column.iSubItem = 2;
    column.cx = 150;
    column.pszText = const_cast<wchar_t*>(text(custom_action_column_text));
    ListView_InsertColumn(custom_hotkey_list_, 2, &column);
    column.iSubItem = 3;
    column.cx = 470;
    column.pszText = const_cast<wchar_t*>(text(custom_target_column_text));
    ListView_InsertColumn(custom_hotkey_list_, 3, &column);
    custom_hotkey_add_button_ = CreateWindowW(L"BUTTON", text(add_text),
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(custom_hotkey_add_identifier)),
        instance_, nullptr);
    custom_hotkey_delete_button_ = CreateWindowW(L"BUTTON", text(delete_text),
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(custom_hotkey_delete_identifier)),
        instance_, nullptr);
    custom_hotkey_edit_button_ = CreateWindowW(L"BUTTON", text(edit_text),
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(custom_hotkey_edit_identifier)),
        instance_, nullptr);
    refresh_custom_hotkey_list();

    menu_icon_heading_ = CreateWindowW(L"STATIC", text(menu_icons_heading_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    menu_icon_scope_ = CreateWindowW(L"STATIC", text(menu_icons_scope_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    menu_icon_list_ = CreateWindowExW(WS_EX_STATICEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(menu_icon_list_identifier)), instance_, nullptr);
    ListView_SetExtendedListViewStyle(menu_icon_list_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    settings_visual_style::style_list_view(menu_icon_list_);
    menu_icon_images_ = ImageList_Create(MulDiv(32, dpi_, 96), MulDiv(32, dpi_, 96),
        ILC_COLOR32 | ILC_MASK, 8, 8);
    ListView_SetImageList(menu_icon_list_, menu_icon_images_, LVSIL_SMALL);
    LVCOLUMNW icon_column{.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM};
    icon_column.cx = 90;
    icon_column.pszText = const_cast<wchar_t*>(text(menu_icon_menu_column_text));
    ListView_InsertColumn(menu_icon_list_, 0, &icon_column);
    icon_column.iSubItem = 1;
    icon_column.cx = 180;
    icon_column.pszText = const_cast<wchar_t*>(text(menu_icon_name_column_text));
    ListView_InsertColumn(menu_icon_list_, 1, &icon_column);
    icon_column.iSubItem = 2;
    icon_column.cx = 400;
    icon_column.pszText = const_cast<wchar_t*>(text(menu_icon_target_column_text));
    ListView_InsertColumn(menu_icon_list_, 2, &icon_column);
    icon_column.iSubItem = 3;
    icon_column.cx = 100;
    icon_column.pszText = const_cast<wchar_t*>(text(menu_icon_source_column_text));
    ListView_InsertColumn(menu_icon_list_, 3, &icon_column);
    menu_icon_select_button_ = CreateWindowW(L"BUTTON", text(select_icon_text),
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(menu_icon_select_identifier)),
        instance_, nullptr);
    menu_icon_restore_button_ = CreateWindowW(L"BUTTON", text(restore_auto_icon_text),
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(menu_icon_restore_identifier)),
        instance_, nullptr);
    refresh_menu_icon_list();

    menu_editor_heading_ = CreateWindowW(L"STATIC", text(quick_launch_heading_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    menu_editor_scope_ = CreateWindowW(L"STATIC", text(quick_launch_scope_text),
        WS_CHILD, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    menu_editor_ = std::make_unique<MenuEditorWindow>(
        instance_, window_, language_, config_directory_ / L"Simpilot.ini",
        config_directory_ / L"Simpilot2.ini",
        [this](const std::wstring_view message) { diagnose(message); },
        [this] { mark_dirty(); });
    if (!menu_editor_->create()) menu_editor_.reset();
    save_button_ = CreateWindowW(L"BUTTON", text(save_text),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(save_identifier)), instance_, nullptr);
    cancel_button_ = CreateWindowW(L"BUTTON", text(cancel_text),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(cancel_identifier)), instance_, nullptr);
    apply_button_ = CreateWindowW(L"BUTTON", text(apply_text),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(apply_identifier)), instance_, nullptr);

    update_fonts();
    update_page_visibility();
    update_apply_enabled();
}

void SettingsWindow::update_fonts() {
    const auto old_font = font_;
    const auto old_section_font = section_font_;
    const auto old_title_font = title_font_;
    font_ = CreateFontW(-MulDiv(13, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    title_font_ = CreateFontW(-MulDiv(20, dpi_, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                              FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    section_font_ = CreateFontW(-MulDiv(16, dpi_, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                                FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    set_font(navigation_, font_);
    set_font(title_, title_font_);
    set_font(general_scope_, font_);
    set_font(startup_section_, section_font_);
    set_font(appearance_section_, section_font_);
    set_font(startup_checkbox_, font_);
    set_font(menu_theme_label_, font_);
    set_font(menu_theme_combo_, font_);
    set_font(hint_, font_);
    set_font(status_, font_);
    set_font(windows_hotkey_heading_, title_font_);
    set_font(windows_hotkey_scope_, font_);
    set_font(windows_hotkey_runtime_, font_);
    set_font(custom_hotkey_heading_, title_font_);
    set_font(custom_hotkey_scope_, font_);
    set_font(built_in_hotkeys_section_, section_font_);
    set_font(custom_hotkeys_section_, section_font_);
    set_font(custom_hotkey_list_, font_);
    set_font(custom_hotkey_add_button_, font_);
    set_font(custom_hotkey_edit_button_, font_);
    set_font(custom_hotkey_delete_button_, font_);
    set_font(menu_icon_heading_, title_font_);
    set_font(menu_icon_scope_, font_);
    set_font(menu_icon_list_, font_);
    set_font(menu_icon_select_button_, font_);
    set_font(menu_icon_restore_button_, font_);
    set_font(menu_editor_heading_, title_font_);
    set_font(menu_editor_scope_, font_);
    set_font(save_button_, font_);
    set_font(apply_button_, font_);
    set_font(cancel_button_, font_);
    for (const auto header : built_in_hotkey_headers_) set_font(header, font_);
    for (std::size_t index = 0; index < capture_buttons_.size(); ++index) {
        set_font(labels_[index], font_);
        set_font(capture_buttons_[index], font_);
        set_font(clear_buttons_[index], font_);
        set_font(built_in_hotkey_switches_[index], font_);
    }
    for (const auto toggle : windows_hotkey_switches_) {
        if (toggle) set_font(toggle, font_);
    }
    if (old_font) DeleteObject(old_font);
    if (old_section_font) DeleteObject(old_section_font);
    if (old_title_font) DeleteObject(old_title_font);
}

void SettingsWindow::layout_controls(const int width, const int height) {
    const auto layout_dpi = std::min(dpi_, std::max(dpi_ * 2 / 3,
        static_cast<UINT>(std::max(1, height) * 96 / 680)));
    const auto scale = [layout_dpi](const int value) {
        return MulDiv(value, layout_dpi, 96);
    };
    const auto horizontal_dpi = std::min(dpi_, std::max(dpi_ * 2 / 3,
        static_cast<UINT>(std::max(1, width) * 96 / 960)));
    const auto wide = [horizontal_dpi](const int value) {
        return MulDiv(value, horizontal_dpi, 96);
    };
    const auto navigation_width = wide(188);
    const auto bottom_height = scale(64);
    const auto content_x = navigation_width + wide(28);
    const auto content_width = std::max(1, width - content_x - wide(28));
    const auto page_title_y = scale(24);
    const auto page_scope_y = scale(62);
    const auto body_top = scale(124);
    const auto body_bottom = height - bottom_height - scale(20);
    auto label_width = wide(176);
    if (const auto dc = GetDC(window_)) {
        const auto previous_font = SelectObject(dc, font_);
        for (const auto label : labels_) {
            const auto length = GetWindowTextLengthW(label);
            std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
            GetWindowTextW(label, value.data(), length + 1);
            SIZE extent{};
            GetTextExtentPoint32W(dc, value.c_str(), length, &extent);
            label_width = std::max(label_width,
                                   static_cast<int>(extent.cx) + wide(12));
        }
        SelectObject(dc, previous_font);
        ReleaseDC(window_, dc);
    }
    label_width = std::min(label_width, content_width * 45 / 100);
    const auto clear_width = wide(90);
    const auto switch_width = wide(64);
    const auto control_height = scale(34);
    const auto edit_x = content_x + label_width;
    const auto edit_width = std::max(wide(150), content_width - label_width
        - clear_width - wide(10));
    SendMessageW(navigation_, LB_SETITEMHEIGHT, 0, scale(42));
    MoveWindow(navigation_, 0, 0, navigation_width, height - bottom_height, TRUE);

    MoveWindow(title_, content_x, page_title_y, content_width, scale(32), TRUE);
    MoveWindow(general_scope_, content_x, page_scope_y, content_width, scale(48), TRUE);
    MoveWindow(startup_section_, content_x, body_top, content_width, scale(28), TRUE);
    MoveWindow(startup_checkbox_, content_x, body_top + scale(36),
               content_width, scale(32), TRUE);
    MoveWindow(appearance_section_, content_x, body_top + scale(104),
               content_width, scale(28), TRUE);
    MoveWindow(menu_theme_label_, content_x, body_top + scale(142),
               label_width - scale(8), control_height, TRUE);
    MoveWindow(menu_theme_combo_, edit_x, body_top + scale(142),
               std::min(wide(280), edit_width), scale(180), TRUE);

    MoveWindow(custom_hotkey_heading_, content_x, page_title_y,
               content_width, scale(32), TRUE);
    MoveWindow(custom_hotkey_scope_, content_x, page_scope_y,
               content_width, scale(48), TRUE);
    MoveWindow(built_in_hotkeys_section_, content_x, body_top,
               content_width, scale(28), TRUE);
    const auto column_gap = wide(10);
    const auto capture_x = content_x + label_width;
    const auto switch_x = content_x + content_width - switch_width;
    const auto action_x = switch_x - column_gap - clear_width;
    const auto capture_width = std::max(wide(150), action_x - column_gap - capture_x);
    const auto header_y = body_top + scale(32);
    MoveWindow(built_in_hotkey_headers_[0], content_x, header_y,
               label_width - wide(8), scale(24), TRUE);
    MoveWindow(built_in_hotkey_headers_[1], capture_x, header_y,
               capture_width, scale(24), TRUE);
    MoveWindow(built_in_hotkey_headers_[2], action_x, header_y,
               clear_width, scale(24), TRUE);
    MoveWindow(built_in_hotkey_headers_[3], switch_x, header_y,
               switch_width, scale(24), TRUE);
    for (std::size_t index = 0; index < capture_buttons_.size(); ++index) {
        const auto y = body_top + scale(58 + static_cast<int>(index) * 50);
        MoveWindow(labels_[index], content_x, y + scale(5),
                   label_width - scale(8), scale(28), TRUE);
        MoveWindow(capture_buttons_[index], capture_x, y,
                   capture_width, scale(42), TRUE);
        MoveWindow(clear_buttons_[index], action_x, y + scale(4),
                   clear_width, scale(34), TRUE);
        MoveWindow(built_in_hotkey_switches_[index], switch_x, y + scale(5),
                   switch_width, scale(32), TRUE);
    }
    const auto hint_y = body_top + scale(262);
    MoveWindow(hint_, content_x, hint_y, content_width, scale(34), TRUE);
    const auto custom_section_y = hint_y + scale(40);
    MoveWindow(custom_hotkeys_section_, content_x, custom_section_y,
               content_width, scale(28), TRUE);
    const auto custom_toolbar_y = custom_section_y + scale(34);
    MoveWindow(custom_hotkey_add_button_, content_x, custom_toolbar_y,
               wide(100), scale(36), TRUE);
    MoveWindow(custom_hotkey_edit_button_, content_x + wide(110), custom_toolbar_y,
               wide(100), scale(36), TRUE);
    MoveWindow(custom_hotkey_delete_button_, content_x + wide(220), custom_toolbar_y,
               wide(100), scale(36), TRUE);
    const auto custom_list_y = custom_toolbar_y + scale(46);
    MoveWindow(custom_hotkey_list_, content_x, custom_list_y,
               content_width, std::max(scale(80), body_bottom - custom_list_y), TRUE);
    const auto enabled_column_width = wide(72);
    const auto hotkey_column_width = wide(170);
    const auto action_column_width = wide(150);
    ListView_SetColumnWidth(custom_hotkey_list_, 0, enabled_column_width);
    ListView_SetColumnWidth(custom_hotkey_list_, 1, hotkey_column_width);
    ListView_SetColumnWidth(custom_hotkey_list_, 2, action_column_width);
    ListView_SetColumnWidth(custom_hotkey_list_, 3,
        std::max(wide(180), content_width - enabled_column_width
            - hotkey_column_width - action_column_width - wide(6)));

    MoveWindow(windows_hotkey_heading_, content_x, page_title_y,
               content_width, scale(32), TRUE);
    MoveWindow(windows_hotkey_scope_, content_x, page_scope_y,
               content_width, scale(48), TRUE);
    const auto windows_column_gap = wide(24);
    const auto checkbox_width = (content_width - windows_column_gap) / 2;
    for (std::size_t visual_index = 0; visual_index < visible_windows_hotkey_count;
         ++visual_index) {
        const auto index = windows_hotkey_index_from_visual(visual_index);
        const auto column = visual_index / 13;
        const auto row = visual_index % 13;
        MoveWindow(windows_hotkey_switches_[index],
            content_x + static_cast<int>(column) * (checkbox_width + windows_column_gap),
            body_top + static_cast<int>(row) * scale(30), checkbox_width, scale(28), TRUE);
    }
    MoveWindow(windows_hotkey_runtime_, content_x, body_top + scale(398),
               content_width, scale(42), TRUE);

    MoveWindow(menu_icon_heading_, content_x, page_title_y,
               content_width, scale(32), TRUE);
    MoveWindow(menu_icon_scope_, content_x, page_scope_y,
               content_width, scale(48), TRUE);
    MoveWindow(menu_icon_select_button_, content_x, body_top,
               wide(132), scale(36), TRUE);
    MoveWindow(menu_icon_restore_button_, content_x + wide(142), body_top,
               wide(170), scale(36), TRUE);
    const auto menu_icon_list_y = body_top + scale(46);
    MoveWindow(menu_icon_list_, content_x, menu_icon_list_y,
               content_width, std::max(scale(100), body_bottom - menu_icon_list_y), TRUE);
    const auto menu_column_width = wide(100);
    const auto name_column_width = wide(180);
    const auto source_column_width = wide(110);
    ListView_SetColumnWidth(menu_icon_list_, 0, menu_column_width);
    ListView_SetColumnWidth(menu_icon_list_, 1, name_column_width);
    ListView_SetColumnWidth(menu_icon_list_, 2,
        std::max(wide(180), content_width - menu_column_width
            - name_column_width - source_column_width - wide(6)));
    ListView_SetColumnWidth(menu_icon_list_, 3, source_column_width);

    MoveWindow(menu_editor_heading_, content_x, page_title_y,
               content_width, scale(32), TRUE);
    MoveWindow(menu_editor_scope_, content_x, page_scope_y,
               content_width, scale(48), TRUE);
    if (menu_editor_) {
        menu_editor_->set_bounds(content_x, body_top, content_width,
                                 std::max(1, body_bottom - body_top));
    }

    MoveWindow(status_, content_x, height - scale(48),
               std::max(wide(100), content_width - wide(340)), scale(32), TRUE);
    MoveWindow(cancel_button_, width - wide(28) - wide(96), height - scale(50),
               wide(96), scale(36), TRUE);
    MoveWindow(apply_button_, width - wide(28) - wide(202), height - scale(50),
               wide(96), scale(36), TRUE);
    MoveWindow(save_button_, width - wide(28) - wide(308), height - scale(50),
               wide(96), scale(36), TRUE);
}

void SettingsWindow::update_page_visibility() {
    const auto general_command = selected_page_ == 0 ? SW_SHOW : SW_HIDE;
    const auto menu_editor_command = selected_page_ == 1 ? SW_SHOW : SW_HIDE;
    const auto menu_icons_command = selected_page_ == 2 ? SW_SHOW : SW_HIDE;
    const auto custom_command = selected_page_ == 3 ? SW_SHOW : SW_HIDE;
    const auto system_command = selected_page_ == 4 ? SW_SHOW : SW_HIDE;
    ShowWindow(title_, general_command);
    ShowWindow(general_scope_, general_command);
    ShowWindow(startup_section_, general_command);
    ShowWindow(appearance_section_, general_command);
    ShowWindow(startup_checkbox_, general_command);
    ShowWindow(menu_theme_label_, general_command);
    ShowWindow(menu_theme_combo_, general_command);

    ShowWindow(menu_editor_heading_, menu_editor_command);
    ShowWindow(menu_editor_scope_, menu_editor_command);
    if (menu_editor_) menu_editor_->set_visible(selected_page_ == 1);

    ShowWindow(windows_hotkey_heading_, system_command);
    ShowWindow(windows_hotkey_scope_, system_command);
    ShowWindow(windows_hotkey_runtime_, system_command);
    for (const auto toggle : windows_hotkey_switches_) {
        if (toggle) ShowWindow(toggle, system_command);
    }
    ShowWindow(custom_hotkey_heading_, custom_command);
    ShowWindow(custom_hotkey_scope_, custom_command);
    ShowWindow(built_in_hotkeys_section_, custom_command);
    for (const auto header : built_in_hotkey_headers_) ShowWindow(header, custom_command);
    ShowWindow(custom_hotkeys_section_, custom_command);
    ShowWindow(hint_, custom_command);
    for (const auto control : labels_) ShowWindow(control, custom_command);
    for (const auto control : capture_buttons_) ShowWindow(control, custom_command);
    for (const auto control : clear_buttons_) ShowWindow(control, custom_command);
    for (const auto control : built_in_hotkey_switches_) ShowWindow(control, custom_command);
    ShowWindow(custom_hotkey_list_, custom_command);
    ShowWindow(custom_hotkey_add_button_, custom_command);
    ShowWindow(custom_hotkey_edit_button_, custom_command);
    ShowWindow(custom_hotkey_delete_button_, custom_command);
    ShowWindow(menu_icon_heading_, menu_icons_command);
    ShowWindow(menu_icon_scope_, menu_icons_command);
    ShowWindow(menu_icon_list_, menu_icons_command);
    ShowWindow(menu_icon_select_button_, menu_icons_command);
    ShowWindow(menu_icon_restore_button_, menu_icons_command);
}

void SettingsWindow::refresh_toggle_state_images() {
    if (!custom_hotkey_list_) return;
    const auto next = toggle_switch::create_state_image_list(dpi_);
    if (!next) return;
    const auto previous = ListView_SetImageList(
        custom_hotkey_list_, next, LVSIL_STATE);
    if (previous) ImageList_Destroy(previous);
    custom_hotkey_state_images_ = next;
}

void SettingsWindow::refresh_custom_hotkey_list(
    const std::optional<std::size_t> selection) {
    if (!custom_hotkey_list_) return;
    refreshing_custom_hotkeys_ = true;
    ListView_DeleteAllItems(custom_hotkey_list_);
    for (std::size_t index = 0; index < settings_.custom_global_hotkeys.size(); ++index) {
        const auto& hotkey = settings_.custom_global_hotkeys[index];
        if (!hotkey.binding.gesture) continue;
        auto gesture = hotkey.binding.gesture->display_text();
        wchar_t empty[] = L"";
        LVITEMW item{
            .mask = LVIF_TEXT | LVIF_PARAM,
            .iItem = static_cast<int>(index),
            .pszText = empty,
            .lParam = static_cast<LPARAM>(index),
        };
        const auto row = ListView_InsertItem(custom_hotkey_list_, &item);
        ListView_SetCheckState(custom_hotkey_list_, row, hotkey.enabled ? TRUE : FALSE);
        ListView_SetItemText(custom_hotkey_list_, row, 1, gesture.data());
        ListView_SetItemText(custom_hotkey_list_, row, 2,
            const_cast<wchar_t*>(text(hotkey.action == CustomHotKeyAction::open_folder
                ? open_folder_text : hotkey.action == CustomHotKeyAction::open_file
                    ? open_file_text : open_application_text)));
        ListView_SetItemText(custom_hotkey_list_, row, 3,
            const_cast<wchar_t*>(hotkey.program_path.c_str()));
        if (selection && *selection == index) {
            ListView_SetItemState(custom_hotkey_list_, row,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(custom_hotkey_list_, row, FALSE);
        }
    }
    refreshing_custom_hotkeys_ = false;
    update_custom_hotkey_buttons();
    refresh_windows_hotkey_linkage();
}

std::optional<std::size_t> SettingsWindow::selected_custom_hotkey_index() const {
    const auto row = ListView_GetNextItem(custom_hotkey_list_, -1, LVNI_SELECTED);
    if (row < 0) return std::nullopt;
    LVITEMW item{.mask = LVIF_PARAM, .iItem = row};
    if (!ListView_GetItem(custom_hotkey_list_, &item)) return std::nullopt;
    const auto index = static_cast<std::size_t>(item.lParam);
    return index < settings_.custom_global_hotkeys.size()
        ? std::optional<std::size_t>(index) : std::nullopt;
}

void SettingsWindow::update_custom_hotkey_buttons() {
    const auto selected = selected_custom_hotkey_index().has_value();
    EnableWindow(custom_hotkey_edit_button_, selected ? TRUE : FALSE);
    EnableWindow(custom_hotkey_delete_button_, selected ? TRUE : FALSE);
}

void SettingsWindow::refresh_menu_icon_list(
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
                ? custom_icon_text : automatic_icon_text)));
        if (selection && *selection == index) {
            ListView_SetItemState(menu_icon_list_, row,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(menu_icon_list_, row, FALSE);
        }
    }
    update_menu_icon_buttons();
}

std::optional<std::size_t> SettingsWindow::selected_menu_icon_index() const {
    const auto row = ListView_GetNextItem(menu_icon_list_, -1, LVNI_SELECTED);
    if (row < 0) return std::nullopt;
    LVITEMW item{.mask = LVIF_PARAM, .iItem = row};
    if (!ListView_GetItem(menu_icon_list_, &item)) return std::nullopt;
    const auto index = static_cast<std::size_t>(item.lParam);
    return index < menu_icon_targets_.size()
        ? std::optional<std::size_t>(index) : std::nullopt;
}

void SettingsWindow::update_menu_icon_buttons() {
    const auto selection = selected_menu_icon_index();
    EnableWindow(menu_icon_select_button_, selection && icon_snapshot_ready_ ? TRUE : FALSE);
    EnableWindow(menu_icon_restore_button_, icon_snapshot_ready_ && selection
        && menu_icon_cache_.has_custom_icon(menu_icon_targets_[*selection].custom_key)
            ? TRUE : FALSE);
}

void SettingsWindow::choose_selected_menu_icon() {
    if (!icon_snapshot_ready_) return;
    const auto selection = selected_menu_icon_index();
    if (!selection) return;

    std::array<wchar_t, 32768> source{};
    wcsncpy_s(source.data(), source.size(),
              menu_icon_targets_[*selection].icon_source.c_str(), _TRUNCATE);
    int source_index = 0;
    if (PickIconDlg(window_, source.data(), static_cast<UINT>(source.size()),
                    &source_index) == -1 || source.front() == L'\0') {
        return;
    }
    if (!menu_icon_cache_.set_custom_icon(menu_icon_targets_[*selection].custom_key,
                                          source.data(), source_index)) {
        MessageBoxW(window_, text(icon_selection_failed_text), text(title_text),
                    MB_OK | MB_ICONWARNING);
        return;
    }
    icon_dirty_ = true;
    mark_dirty();
    refresh_menu_icon_list(*selection);
}

void SettingsWindow::restore_selected_menu_icon() {
    const auto selection = selected_menu_icon_index();
    if (!selection || !menu_icon_cache_.remove_custom_icon(
            menu_icon_targets_[*selection].custom_key)) {
        return;
    }
    icon_dirty_ = true;
    mark_dirty();
    refresh_menu_icon_list(*selection);
}

void SettingsWindow::add_custom_hotkey() {
    if (capturing_) cancel_capture();
    auto candidate = CustomHotKeyDialog::show_modal(
        instance_, window_, language_, keyboard_manager_, config_directory_, nullptr,
        [this](const std::wstring_view message) { diagnose(message); });
    if (candidate && commit_custom_hotkey(std::move(*candidate), std::nullopt)) mark_dirty();
}

void SettingsWindow::edit_selected_custom_hotkey() {
    if (capturing_) cancel_capture();
    const auto index = selected_custom_hotkey_index();
    if (!index) return;
    auto candidate = CustomHotKeyDialog::show_modal(
        instance_, window_, language_, keyboard_manager_, config_directory_,
        &settings_.custom_global_hotkeys[*index],
        [this](const std::wstring_view message) { diagnose(message); });
    if (candidate && commit_custom_hotkey(std::move(*candidate), index)) mark_dirty();
}

bool SettingsWindow::commit_custom_hotkey(
    CustomGlobalHotKey candidate, std::optional<std::size_t> editing_index) {
    if (!candidate.binding.gesture) return false;
    if (editing_index && *editing_index >= settings_.custom_global_hotkeys.size()) return false;
    read_windows_hotkey_controls();
    const auto gesture = *candidate.binding.gesture;
    if (const auto windows_index = windows_letter_hotkey_index(gesture);
        windows_index && *windows_index == unsupported_windows_hotkey_index) {
        return false;
    }
    const auto gesture_changed = !editing_index
        || !settings_.custom_global_hotkeys[*editing_index].binding.gesture
        || *settings_.custom_global_hotkeys[*editing_index].binding.gesture != gesture;

    std::array<bool, 4> clear_fixed{};
    const std::array fixed_bindings{
        std::pair{&settings_.main_menu.binding, field_name(0)},
        std::pair{&settings_.second_menu.binding, field_name(1)},
        std::pair{&settings_.open_settings.binding, field_name(2)},
        std::pair{&settings_.everything_search.binding, field_name(3)},
    };
    for (std::size_t index = 0; gesture_changed && index < fixed_bindings.size(); ++index) {
        auto* binding_value = fixed_bindings[index].first;
        if (!binding_value->gesture || *binding_value->gesture != gesture) continue;
        const auto message = language_ == UiLanguage::simplified_chinese
            ? std::format(L"{} 已由“{}”使用。是否清除原设置并改为此自定义操作？",
                          gesture.display_text(), fixed_bindings[index].second)
            : std::format(L"{} is already used by '{}'. Clear that assignment and use this custom action?",
                          gesture.display_text(), fixed_bindings[index].second);
        if (MessageBoxW(window_, message.c_str(), text(title_text),
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
        clear_fixed[index] = true;
    }

    std::optional<std::size_t> duplicate_index;
    for (std::size_t index = 0;
         gesture_changed && index < settings_.custom_global_hotkeys.size(); ++index) {
        if (editing_index && *editing_index == index) continue;
        const auto& item = settings_.custom_global_hotkeys[index];
        if (item.binding.gesture && *item.binding.gesture == gesture) {
            duplicate_index = index;
            break;
        }
    }
    if (duplicate_index) {
        const auto message = language_ == UiLanguage::simplified_chinese
            ? std::format(L"{} 已存在自定义操作。是否替换原操作？", gesture.display_text())
            : std::format(L"A custom action already uses {}. Replace it?", gesture.display_text());
        if (MessageBoxW(window_, message.c_str(), text(title_text),
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
    }

    if (gesture_changed) candidate.binding.force_override = false;
    if (gesture_changed && is_supported_windows_letter_hotkey(gesture)) {
        candidate.binding.force_override = true;
    } else if (gesture_changed && availability_probe_ && !availability_probe_(gesture)) {
        const auto message = language_ == UiLanguage::simplified_chinese
            ? std::format(L"{} 已被 Windows 或其他程序注册。是否尝试强制覆盖？",
                          gesture.display_text())
            : std::format(L"{} is registered by Windows or another application. Try to force an override?",
                          gesture.display_text());
        if (MessageBoxW(window_, message.c_str(), text(title_text),
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
        candidate.binding.force_override = true;
    }

    auto next = settings_;
    for (std::size_t index = 0; index < clear_fixed.size(); ++index) {
        if (!clear_fixed[index]) continue;
        auto* hotkey = index == 0 ? &next.main_menu
            : index == 1 ? &next.second_menu
            : index == 2 ? &next.open_settings
            : &next.everything_search;
        hotkey->binding = {};
        hotkey->enabled = false;
    }
    std::size_t selected_index = 0;
    if (!editing_index && duplicate_index) {
        selected_index = *duplicate_index;
        next.custom_global_hotkeys[selected_index] = std::move(candidate);
    } else if (editing_index) {
        auto target = *editing_index;
        if (duplicate_index) {
            next.custom_global_hotkeys.erase(next.custom_global_hotkeys.begin()
                + static_cast<std::ptrdiff_t>(*duplicate_index));
            if (*duplicate_index < target) --target;
        }
        selected_index = target;
        next.custom_global_hotkeys[target] = std::move(candidate);
    } else {
        selected_index = next.custom_global_hotkeys.size();
        next.custom_global_hotkeys.push_back(std::move(candidate));
    }
    settings_ = std::move(next);
    for (std::size_t index = 0; index < capture_buttons_.size(); ++index) {
        update_built_in_hotkey_control(index);
    }
    refresh_custom_hotkey_list(selected_index);
    return true;
}

void SettingsWindow::delete_selected_custom_hotkey() {
    const auto index = selected_custom_hotkey_index();
    if (!index) return;
    const auto message = language_ == UiLanguage::simplified_chinese
        ? L"确定要删除选中的自定义全局热键吗？"
        : L"Delete the selected custom global hotkey?";
    if (MessageBoxW(window_, message, text(title_text),
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    read_windows_hotkey_controls();
    settings_.custom_global_hotkeys.erase(settings_.custom_global_hotkeys.begin()
        + static_cast<std::ptrdiff_t>(*index));
    refresh_custom_hotkey_list();
    mark_dirty();
}

void SettingsWindow::refresh_windows_hotkey_linkage() {
    for (std::size_t index = 0; index < windows_hotkey_switches_.size(); ++index) {
        const auto toggle = windows_hotkey_switches_[index];
        if (!toggle || index == unsupported_windows_hotkey_index) continue;
        const auto required = global_hotkey_requires_windows_blocking(settings_, index);
        auto label = windows_hotkey_label(index);
        if (required) {
            label += language_ == UiLanguage::simplified_chinese
                ? L"（由全局热键启用）" : L" (enabled by a global hotkey)";
        }
        SetWindowTextW(toggle, label.c_str());
        SendMessageW(toggle, BM_SETCHECK,
            settings_.disabled_windows_hotkeys[index] || required
                ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(toggle, required ? FALSE : TRUE);
    }
}

void SettingsWindow::read_windows_hotkey_controls() {
    settings_.disabled_windows_hotkeys[unsupported_windows_hotkey_index] = false;
    for (std::size_t index = 0; index < windows_hotkey_switches_.size(); ++index) {
        if (index == unsupported_windows_hotkey_index || !windows_hotkey_switches_[index]
            || global_hotkey_requires_windows_blocking(settings_, index)) continue;
        settings_.disabled_windows_hotkeys[index] =
            SendMessageW(windows_hotkey_switches_[index], BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
}

void SettingsWindow::begin_capture(const std::size_t index) {
    if (index >= capture_buttons_.size()) return;
    if (capturing_) {
        if (*capturing_ == index) return;
        cancel_capture();
    }
    capturing_ = index;
    capture_original_ = binding(index);
    diagnose(std::format(L"hotkey capture begin field=\"{}\" uiThread={}",
                         field_name(index), GetCurrentThreadId()));
    if (!keyboard_manager_.begin_capture(
            [this, index](const KeyboardCaptureResult& result) {
                handle_capture_result(index, result);
            })) {
        const auto error = keyboard_manager_.last_error();
        diagnose(std::format(L"hotkey capture activation failed error={} message=\"{}\"",
                             error, windows_error_message(error)));
        capturing_.reset();
        update_binding_text(index);
        SetWindowTextW(status_, text(capture_failed_text));
        const auto detail = language_ == UiLanguage::simplified_chinese
            ? std::format(L"无法启用专用键盘线程的录制状态。\n\nWindows 错误 {}：{}\n\n"
                          L"详细信息已写入 Log\\Simpilot.log。",
                          error, windows_error_message(error))
            : std::format(L"The dedicated keyboard thread could not enter capture mode.\n\n"
                          L"Windows error {}: {}\n\n"
                          L"Details were written to Log\\Simpilot.log.",
                          error, windows_error_message(error));
        MessageBoxW(window_, detail.c_str(), text(title_text), MB_OK | MB_ICONERROR);
        return;
    }
    update_binding_text(index);
    SetWindowTextW(status_, text(capture_text));
}

void SettingsWindow::cancel_capture() {
    if (!capturing_) return;
    const auto index = *capturing_;
    keyboard_manager_.end_capture();
    diagnose(L"hotkey capture cancelled");
    capturing_.reset();
    update_binding_text(index);
    SetWindowTextW(status_, text(escape_cancelled_text));
}

void SettingsWindow::complete_capture(const std::size_t index,
                                      const HotKeyGesture& gesture) {
    if (!capturing_ || *capturing_ != index) return;
    keyboard_manager_.end_capture();
    diagnose(std::format(L"hotkey capture completed gesture=\"{}\"", gesture.display_text()));
    if (capture_original_.gesture && *capture_original_.gesture == gesture) {
        capturing_.reset();
        update_binding_text(index);
        SetWindowTextW(status_, L"");
        return;
    }
    if (const auto windows_index = windows_letter_hotkey_index(gesture);
        windows_index && *windows_index == unsupported_windows_hotkey_index) {
        MessageBoxW(window_, text(win_l_unsupported_text), text(title_text),
                    MB_OK | MB_ICONWARNING);
        cancel_capture();
        return;
    }

    std::array<bool, 4> clear_built_in{};
    for (std::size_t other = 0; other < capture_buttons_.size(); ++other) {
        if (other == index || !binding(other).gesture || *binding(other).gesture != gesture) continue;
        const auto message = language_ == UiLanguage::simplified_chinese
            ? std::format(L"{} \u5df2\u7531\u201c{}\u201d\u4f7f\u7528\u3002\u662f\u5426\u8986\u76d6\u539f\u8bbe\u7f6e\uff1f",
                          gesture.display_text(), field_name(other))
            : std::format(L"{} is already used by '{}'. Override the existing assignment?",
                          gesture.display_text(), field_name(other));
        if (MessageBoxW(window_, message.c_str(), text(title_text),
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            cancel_capture();
            return;
        }
        clear_built_in[other] = true;
    }

    const auto custom = std::ranges::find_if(settings_.custom_global_hotkeys,
        [&gesture](const CustomGlobalHotKey& hotkey) {
            return hotkey.binding.gesture && *hotkey.binding.gesture == gesture;
        });
    std::optional<std::size_t> custom_index;
    if (custom != settings_.custom_global_hotkeys.end()) {
        custom_index = static_cast<std::size_t>(
            std::distance(settings_.custom_global_hotkeys.begin(), custom));
        const auto message = language_ == UiLanguage::simplified_chinese
            ? std::format(L"{} 已由一个自定义全局热键使用。是否删除原自定义热键并改为此固定热键？",
                          gesture.display_text())
            : std::format(L"{} is used by a custom global hotkey. Delete it and use this fixed hotkey?",
                          gesture.display_text());
        if (MessageBoxW(window_, message.c_str(), text(title_text),
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            cancel_capture();
            return;
        }
    }

    HotKeyBinding new_binding{gesture, false};
    if (is_supported_windows_letter_hotkey(gesture)) {
        new_binding.force_override = true;
    } else if (availability_probe_ && !availability_probe_(gesture)) {
        const auto message = language_ == UiLanguage::simplified_chinese
            ? std::format(L"{} \u5df2\u88ab Windows \u6216\u5176\u4ed6\u7a0b\u5e8f\u6ce8\u518c\u3002\u662f\u5426\u5c1d\u8bd5\u5f3a\u5236\u8986\u76d6\uff1f\n\n\u6ce8\u610f\uff1aCtrl+Alt+Del \u7b49\u5b89\u5168\u7ec4\u5408\u65e0\u6cd5\u8986\u76d6\u3002",
                          gesture.display_text())
            : std::format(L"{} is registered by Windows or another application. Try to force an override?\n\nSecurity combinations such as Ctrl+Alt+Del cannot be overridden.",
                          gesture.display_text());
        if (MessageBoxW(window_, message.c_str(), text(title_text),
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            cancel_capture();
            return;
        }
        new_binding.force_override = true;
    }

    auto next = settings_;
    for (std::size_t other = 0; other < clear_built_in.size(); ++other) {
        if (!clear_built_in[other]) continue;
        auto* hotkey = other == 0 ? &next.main_menu
            : other == 1 ? &next.second_menu
            : other == 2 ? &next.open_settings
            : &next.everything_search;
        hotkey->binding = {};
        hotkey->enabled = false;
    }
    if (custom_index) {
        next.custom_global_hotkeys.erase(next.custom_global_hotkeys.begin()
            + static_cast<std::ptrdiff_t>(*custom_index));
    }
    auto* target = index == 0 ? &next.main_menu
        : index == 1 ? &next.second_menu
        : index == 2 ? &next.open_settings
        : &next.everything_search;
    target->binding = new_binding;
    settings_ = std::move(next);
    capturing_.reset();
    for (std::size_t item = 0; item < capture_buttons_.size(); ++item) {
        update_built_in_hotkey_control(item);
    }
    if (custom_index) refresh_custom_hotkey_list();
    refresh_windows_hotkey_linkage();
    SetWindowTextW(status_, L"");
    mark_dirty();
}

void SettingsWindow::handle_capture_result(
    const std::size_t index, const KeyboardCaptureResult& result) {
    if (!capturing_ || *capturing_ != index) return;
    if (result.kind == KeyboardCaptureResultKind::cancelled) {
        cancel_capture();
    } else {
        complete_capture(index, result.gesture);
    }
    if (window_) SetFocus(window_);
}

void SettingsWindow::clear_binding(const std::size_t index) {
    if (capturing_) cancel_capture();
    if (index >= capture_buttons_.size()) return;
    auto& hotkey = built_in_hotkey(index);
    if (!hotkey.binding.gesture && !hotkey.enabled) return;
    hotkey.binding = {};
    hotkey.enabled = false;
    update_built_in_hotkey_control(index);
    refresh_windows_hotkey_linkage();
    SetWindowTextW(status_, L"");
    mark_dirty();
}

void SettingsWindow::update_binding_text(const std::size_t index) {
    const auto value = hotkey_capture_button_text(
        binding(index).gesture, capturing_ && *capturing_ == index, language_);
    SetWindowTextW(capture_buttons_[index], value.c_str());
}

void SettingsWindow::update_built_in_hotkey_control(const std::size_t index) {
    if (index >= capture_buttons_.size()) return;
    const auto& hotkey = built_in_hotkey(index);
    update_binding_text(index);
    SendMessageW(built_in_hotkey_switches_[index], BM_SETCHECK,
                 hotkey.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(built_in_hotkey_switches_[index], hotkey.binding.gesture ? TRUE : FALSE);
    EnableWindow(clear_buttons_[index], hotkey.binding.gesture ? TRUE : FALSE);
}

BuiltInHotKey& SettingsWindow::built_in_hotkey(const std::size_t index) {
    if (index == 0) return settings_.main_menu;
    if (index == 1) return settings_.second_menu;
    if (index == 2) return settings_.open_settings;
    return settings_.everything_search;
}

const BuiltInHotKey& SettingsWindow::built_in_hotkey(const std::size_t index) const {
    if (index == 0) return settings_.main_menu;
    if (index == 1) return settings_.second_menu;
    if (index == 2) return settings_.open_settings;
    return settings_.everything_search;
}

HotKeyBinding& SettingsWindow::binding(const std::size_t index) {
    return built_in_hotkey(index).binding;
}

const HotKeyBinding& SettingsWindow::binding(const std::size_t index) const {
    return built_in_hotkey(index).binding;
}

const wchar_t* SettingsWindow::field_name(const std::size_t index) const noexcept {
    return text(index == 0 ? main_menu_text
        : index == 1 ? second_menu_text
        : index == 2 ? open_settings_text
        : open_everything_search_text);
}

std::wstring SettingsWindow::windows_hotkey_label(const std::size_t index) const {
    static constexpr std::array chinese{
        L"快速设置", L"聚焦通知区域", L"Copilot 或搜索", L"显示或隐藏桌面",
        L"文件资源管理器", L"反馈中心", L"Xbox 游戏栏", L"语音输入",
        L"Windows 设置", L"Recall（受支持设备）", L"投放", L"锁定电脑",
        L"最小化所有窗口", L"通知中心和日历", L"锁定屏幕方向", L"投影模式",
        L"搜索", L"运行", L"搜索", L"切换任务栏应用", L"辅助功能设置",
        L"剪贴板历史记录", L"小组件", L"快捷链接菜单", L"混合现实输入切换",
        L"贴靠布局",
    };
    static constexpr std::array english{
        L"Quick Settings", L"Focus notification area", L"Copilot or Search",
        L"Show or hide desktop", L"File Explorer", L"Feedback Hub",
        L"Xbox Game Bar", L"Voice typing", L"Windows Settings",
        L"Recall (supported devices)", L"Cast", L"Lock PC",
        L"Minimize all windows", L"Notification Center and calendar",
        L"Lock screen orientation", L"Project display mode", L"Search", L"Run",
        L"Search", L"Cycle taskbar apps", L"Accessibility settings",
        L"Clipboard history", L"Widgets", L"Quick Link menu",
        L"Mixed Reality input switching", L"Snap layouts",
    };
    const auto letter = static_cast<wchar_t>(L'A' + index);
    const auto* description = language_ == UiLanguage::simplified_chinese
        ? chinese[index] : english[index];
    return std::format(L"Win+{}  —  {}", letter, description);
}

void SettingsWindow::diagnose(const std::wstring_view message) const noexcept {
    if (!diagnostic_sink_) return;
    try {
        diagnostic_sink_(message);
    } catch (...) {
    }
}

void SettingsWindow::mark_dirty() {
    SetWindowTextW(status_, L"");
    update_apply_enabled();
}

bool SettingsWindow::has_unsaved_changes() const {
    return settings_ != applied_settings_ || icon_dirty_
        || (menu_editor_ && menu_editor_->dirty());
}

void SettingsWindow::update_apply_enabled() {
    if (apply_button_) EnableWindow(apply_button_, has_unsaved_changes() ? TRUE : FALSE);
}

bool SettingsWindow::request_close() {
    if (!has_unsaved_changes()) return true;
    if (MessageBoxW(window_, text(unsaved_changes_text), text(title_text),
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return false;
    }
    icon_dirty_ = false;
    settings_ = applied_settings_;
    return true;
}

void SettingsWindow::create_icon_snapshot() {
    icon_snapshot_ready_ = false;
    std::error_code error;
    std::filesystem::remove_all(icon_snapshot_directory_, error);
    error.clear();
    std::filesystem::create_directories(icon_snapshot_directory_, error);
    if (error) {
        diagnose(L"menu icon draft directory creation failed");
        return;
    }
    if (!std::filesystem::is_directory(icon_cache_directory_, error)) {
        icon_snapshot_ready_ = !error;
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

bool SettingsWindow::commit_icon_snapshot() {
    if (!icon_snapshot_ready_) return false;
    const auto rollback_directory = icon_snapshot_directory_ / L"Rollback";
    std::error_code error;
    std::filesystem::remove_all(rollback_directory, error);
    error.clear();
    std::filesystem::create_directories(rollback_directory, error);
    if (error) return false;
    if (std::filesystem::is_directory(icon_cache_directory_, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(icon_cache_directory_, error)) {
            if (error) break;
            if (!entry.is_regular_file(error)
                || !entry.path().filename().wstring().ends_with(L".custom.ico")) continue;
            std::filesystem::copy_file(entry.path(),
                rollback_directory / entry.path().filename(),
                std::filesystem::copy_options::overwrite_existing, error);
            if (error) break;
        }
    }
    if (!error) {
        std::filesystem::create_directories(icon_cache_directory_, error);
    }
    if (!error) {
        for (const auto& entry : std::filesystem::directory_iterator(icon_cache_directory_, error)) {
            if (error) break;
            if (entry.is_regular_file(error)
                && entry.path().filename().wstring().ends_with(L".custom.ico")) {
                std::filesystem::remove(entry.path(), error);
                if (error) break;
            }
        }
    }
    if (!error) {
        for (const auto& entry : std::filesystem::directory_iterator(icon_snapshot_directory_, error)) {
            if (error) break;
            if (!entry.is_regular_file(error)
                || !entry.path().filename().wstring().ends_with(L".custom.ico")) continue;
            std::filesystem::copy_file(entry.path(), icon_cache_directory_ / entry.path().filename(),
                std::filesystem::copy_options::overwrite_existing, error);
            if (error) break;
        }
    }
    if (!error) {
        std::filesystem::remove_all(rollback_directory, error);
        return true;
    }

    std::error_code rollback_error;
    for (const auto& entry : std::filesystem::directory_iterator(icon_cache_directory_, rollback_error)) {
        if (rollback_error) break;
        if (entry.is_regular_file(rollback_error)
            && entry.path().filename().wstring().ends_with(L".custom.ico")) {
            std::filesystem::remove(entry.path(), rollback_error);
            rollback_error.clear();
        }
    }
    rollback_error.clear();
    for (const auto& entry : std::filesystem::directory_iterator(rollback_directory, rollback_error)) {
        if (rollback_error) break;
        if (!entry.is_regular_file(rollback_error)) continue;
        std::filesystem::copy_file(entry.path(), icon_cache_directory_ / entry.path().filename(),
            std::filesystem::copy_options::overwrite_existing, rollback_error);
        rollback_error.clear();
    }
    diagnose(L"menu icon draft commit failed");
    return false;
}

void SettingsWindow::discard_icon_snapshot() noexcept {
    if (icon_snapshot_directory_.empty()) return;
    std::error_code error;
    std::filesystem::remove_all(icon_snapshot_directory_, error);
    icon_snapshot_ready_ = false;
}

bool SettingsWindow::apply_current() {
    if (capturing_) cancel_capture();
    settings_.start_with_windows = SendMessageW(startup_checkbox_, BM_GETCHECK, 0, 0)
        == BST_CHECKED;
    settings_.menu_theme = static_cast<MenuTheme>(std::max<LRESULT>(0,
        SendMessageW(menu_theme_combo_, CB_GETCURSEL, 0, 0)));
    read_windows_hotkey_controls();
    const auto menu_changed = menu_editor_ && menu_editor_->dirty();
    if (menu_editor_ && !menu_editor_->apply()) return false;
    if (menu_changed && menu_change_sink_) {
        menu_icon_targets_ = menu_change_sink_();
        refresh_menu_icon_list();
    }
    if (apply_sink_ && !apply_sink_(settings_)) return false;
    if (icon_dirty_ && !commit_icon_snapshot()) {
        MessageBoxW(window_, text(icon_selection_failed_text), text(title_text),
                    MB_OK | MB_ICONWARNING);
        return false;
    }
    if (icon_dirty_ && icon_change_sink_) icon_change_sink_();
    applied_settings_ = settings_;
    icon_dirty_ = false;
    result_ = settings_;
    create_icon_snapshot();
    SetWindowTextW(status_, text(applied_text));
    update_apply_enabled();
    return true;
}

const wchar_t* SettingsWindow::text(const SettingsText identifier) const noexcept {
    return Localization(language_).text(identifier).data();
}

LRESULT CALLBACK SettingsWindow::window_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* settings = static_cast<SettingsWindow*>(creation->lpCreateParams);
        settings->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto* settings = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return settings ? settings->handle_message(message, wparam, lparam)
                    : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT SettingsWindow::handle_message(const UINT message,
                                       const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_ERASEBKGND) {
        return settings_visual_style::erase_background(window_, wparam);
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window_, &paint);
        RECT client{};
        GetClientRect(window_, &client);
        const auto layout_dpi = std::min(dpi_, std::max(dpi_ * 2 / 3,
            static_cast<UINT>(std::max(1L, client.bottom) * 96 / 680)));
        const auto horizontal_dpi = std::min(dpi_, std::max(dpi_ * 2 / 3,
            static_cast<UINT>(std::max(1L, client.right) * 96 / 960)));
        const auto pen = CreatePen(PS_SOLID, 1,
            settings_visual_style::high_contrast_enabled()
                ? GetSysColor(COLOR_3DSHADOW) : RGB(213, 217, 221));
        const auto previous = SelectObject(dc, pen);
        const auto navigation_x = MulDiv(188, horizontal_dpi, 96);
        const auto bottom_y = client.bottom - MulDiv(64, layout_dpi, 96);
        MoveToEx(dc, navigation_x, client.top, nullptr);
        LineTo(dc, navigation_x, bottom_y);
        MoveToEx(dc, client.left, bottom_y, nullptr);
        LineTo(dc, client.right, bottom_y);
        SelectObject(dc, previous);
        DeleteObject(pen);
        EndPaint(window_, &paint);
        return 0;
    }
    if (message == WM_CTLCOLORSTATIC) {
        const auto control = reinterpret_cast<HWND>(lparam);
        if (control == general_scope_ || control == hint_ || control == status_
            || control == menu_editor_scope_ || control == windows_hotkey_scope_
            || control == windows_hotkey_runtime_ || control == custom_hotkey_scope_
            || control == menu_icon_scope_) {
            return settings_visual_style::handle_secondary_text(wparam, lparam);
        }
    }
    if (message == WM_CTLCOLORLISTBOX
        && reinterpret_cast<HWND>(lparam) == navigation_) {
        return settings_visual_style::handle_navigation_color(wparam);
    }
    if (settings_visual_style::is_color_message(message)) {
        return settings_visual_style::handle_color_message(message, wparam, lparam);
    }
    if (message == WM_CREATE) {
        create_controls();
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        layout_controls(rectangle.right, rectangle.bottom);
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto current_dpi = window_ ? GetDpiForWindow(window_) : dpi_;
        MONITORINFO monitor_info{.cbSize = sizeof(monitor_info)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor_info);
        const auto work_width = monitor_info.rcWork.right - monitor_info.rcWork.left;
        const auto work_height = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
        limits->ptMinTrackSize.x = std::min(MulDiv(960, current_dpi, 96),
                                            static_cast<int>(work_width * 92 / 100));
        limits->ptMinTrackSize.y = std::min(MulDiv(680, current_dpi, 96),
                                            static_cast<int>(work_height * 92 / 100));
        return 0;
    }
    if (message == WM_DRAWITEM) {
        const auto* drawing = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (drawing && drawing->CtlID == navigation_identifier) {
            settings_visual_style::draw_navigation_item(*drawing, navigation_);
            return TRUE;
        }
    }
    if (message == WM_SIZE) {
        layout_controls(LOWORD(lparam), HIWORD(lparam));
        return 0;
    }
    if (message == WM_DPICHANGED) {
        dpi_ = HIWORD(wparam);
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        update_fonts();
        refresh_toggle_state_images();
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        layout_controls(rectangle.right, rectangle.bottom);
        return 0;
    }
    if (message == WM_THEMECHANGED || message == WM_SETTINGCHANGE) {
        refresh_toggle_state_images();
        for (const auto toggle : built_in_hotkey_switches_) {
            if (toggle) InvalidateRect(toggle, nullptr, TRUE);
        }
        for (const auto toggle : windows_hotkey_switches_) {
            if (toggle) InvalidateRect(toggle, nullptr, TRUE);
        }
        InvalidateRect(window_, nullptr, TRUE);
        return DefWindowProcW(window_, message, wparam, lparam);
    }
    if (message == WM_NOTIFY) {
        const auto* notification = reinterpret_cast<const NMHDR*>(lparam);
        if (notification && notification->hwndFrom == custom_hotkey_list_) {
            if (notification->code == LVN_ITEMCHANGED) {
                update_custom_hotkey_buttons();
                if (refreshing_custom_hotkeys_) return 0;
                const auto* changed = reinterpret_cast<const NMLISTVIEW*>(lparam);
                if ((changed->uChanged & LVIF_STATE) != 0
                    && ((changed->uOldState ^ changed->uNewState)
                        & LVIS_STATEIMAGEMASK) != 0
                    && changed->iItem >= 0) {
                    LVITEMW item{.mask = LVIF_PARAM, .iItem = changed->iItem};
                    if (ListView_GetItem(custom_hotkey_list_, &item)) {
                        const auto index = static_cast<std::size_t>(item.lParam);
                        if (index < settings_.custom_global_hotkeys.size()) {
                            read_windows_hotkey_controls();
                            settings_.custom_global_hotkeys[index].enabled =
                                ListView_GetCheckState(custom_hotkey_list_, changed->iItem) != FALSE;
                            refresh_windows_hotkey_linkage();
                            mark_dirty();
                        }
                    }
                }
                return 0;
            }
            if (notification->code == NM_DBLCLK) {
                const auto* activated = reinterpret_cast<const NMITEMACTIVATE*>(lparam);
                if (activated->iItem >= 0 && activated->iSubItem != 0) {
                    edit_selected_custom_hotkey();
                }
                return 0;
            }
        }
        if (notification && notification->hwndFrom == menu_icon_list_) {
            if (notification->code == LVN_ITEMCHANGED) {
                update_menu_icon_buttons();
                return 0;
            }
            if (notification->code == NM_DBLCLK) {
                const auto* activated = reinterpret_cast<const NMITEMACTIVATE*>(lparam);
                if (activated->iItem >= 0) choose_selected_menu_icon();
                return 0;
            }
        }
    }
    if (message == WM_COMMAND) {
        const auto identifier = LOWORD(wparam);
        const auto notification = HIWORD(wparam);
        if (identifier == navigation_identifier && notification == LBN_SELCHANGE) {
            if (capturing_) cancel_capture();
            selected_page_ = static_cast<int>(SendMessageW(
                navigation_, LB_GETCURSEL, 0, 0));
            if (selected_page_ < 0) selected_page_ = 0;
            update_page_visibility();
            return 0;
        }
        if (identifier == startup_identifier && notification == BN_CLICKED) {
            settings_.start_with_windows = SendMessageW(
                startup_checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            mark_dirty();
            return 0;
        }
        if (identifier >= edit_base_identifier
            && identifier < edit_base_identifier
                + static_cast<int>(capture_buttons_.size())
            && notification == BN_CLICKED) {
            begin_capture(static_cast<std::size_t>(identifier - edit_base_identifier));
            return 0;
        }
        if (identifier >= built_in_hotkey_switch_base_identifier
            && identifier < built_in_hotkey_switch_base_identifier
                + static_cast<int>(built_in_hotkey_switches_.size())
            && notification == BN_CLICKED) {
            const auto index = static_cast<std::size_t>(
                identifier - built_in_hotkey_switch_base_identifier);
            auto& hotkey = built_in_hotkey(index);
            hotkey.enabled = hotkey.binding.gesture
                && SendMessageW(built_in_hotkey_switches_[index],
                                BM_GETCHECK, 0, 0) == BST_CHECKED;
            update_built_in_hotkey_control(index);
            refresh_windows_hotkey_linkage();
            mark_dirty();
            return 0;
        }
        if (identifier == menu_theme_identifier && notification == CBN_SELCHANGE) {
            settings_.menu_theme = static_cast<MenuTheme>(std::max<LRESULT>(0,
                SendMessageW(menu_theme_combo_, CB_GETCURSEL, 0, 0)));
            mark_dirty();
            return 0;
        }
        if (identifier >= windows_hotkey_base_identifier
            && identifier < windows_hotkey_base_identifier + 26
            && notification == BN_CLICKED) {
            read_windows_hotkey_controls();
            mark_dirty();
            return 0;
        }
        if ((identifier == save_identifier || identifier == apply_identifier)
            && notification == BN_CLICKED) {
            if (apply_current() && identifier == save_identifier) DestroyWindow(window_);
            return 0;
        }
        if (identifier == cancel_identifier && notification == BN_CLICKED) {
            if (request_close()) DestroyWindow(window_);
            return 0;
        }
        if (identifier >= clear_base_identifier && identifier < clear_base_identifier + 4
            && notification == BN_CLICKED) {
            clear_binding(static_cast<std::size_t>(identifier - clear_base_identifier));
            return 0;
        }
        if (identifier == custom_hotkey_add_identifier && HIWORD(wparam) == BN_CLICKED) {
            add_custom_hotkey();
            return 0;
        }
        if (identifier == custom_hotkey_edit_identifier && HIWORD(wparam) == BN_CLICKED) {
            edit_selected_custom_hotkey();
            return 0;
        }
        if (identifier == custom_hotkey_delete_identifier && HIWORD(wparam) == BN_CLICKED) {
            delete_selected_custom_hotkey();
            return 0;
        }
        if (identifier == menu_icon_select_identifier && HIWORD(wparam) == BN_CLICKED) {
            choose_selected_menu_icon();
            return 0;
        }
        if (identifier == menu_icon_restore_identifier && HIWORD(wparam) == BN_CLICKED) {
            restore_selected_menu_icon();
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        if (request_close()) DestroyWindow(window_);
        return 0;
    }
    if (message == WM_DESTROY) {
        keyboard_manager_.end_capture();
        if (custom_hotkey_list_) {
            ListView_SetImageList(custom_hotkey_list_, nullptr, LVSIL_STATE);
        }
        if (custom_hotkey_state_images_) ImageList_Destroy(custom_hotkey_state_images_);
        custom_hotkey_state_images_ = nullptr;
        if (menu_icon_images_) ImageList_Destroy(menu_icon_images_);
        menu_icon_images_ = nullptr;
        window_ = nullptr;
        return 0;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

} // namespace simpilot
