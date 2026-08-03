#include "custom_hotkey_dialog.hpp"

#include "hotkey_capture_button.hpp"
#include "resource.h"
#include "settings_visual_style.hpp"

#include "simpilot/variable_expander.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string>

namespace simpilot {
namespace {

constexpr auto dialog_class_name = L"Simpilot.CustomHotKeyDialog";
constexpr int capture_identifier = 100;
constexpr int allow_modifiers_identifier = 101;
constexpr int program_edit_identifier = 200;
constexpr int program_browse_identifier = 201;
constexpr int working_directory_browse_identifier = 202;
constexpr int action_type_identifier = 203;
constexpr int save_identifier = 1;
constexpr int cancel_identifier = 2;
using enum CustomHotKeyText;

void set_font(const HWND control, const HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring control_text(const HWND control) {
    const auto length = GetWindowTextLengthW(control);
    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(length));
    return result;
}

void add_combo_item(const HWND combo, const wchar_t* value) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
}

} // namespace

std::optional<CustomGlobalHotKey> CustomHotKeyDialog::show_modal(
    const HINSTANCE instance, const HWND owner, const UiLanguage language,
    KeyboardManager& keyboard_manager, std::filesystem::path config_directory,
    const CustomGlobalHotKey* initial,
    DiagnosticSink diagnostic_sink) {
    CustomHotKeyDialog dialog(
        instance, owner, language, keyboard_manager, std::move(config_directory), initial,
        std::move(diagnostic_sink));
    return dialog.run();
}

CustomHotKeyDialog::CustomHotKeyDialog(
    const HINSTANCE instance, const HWND owner, const UiLanguage language,
    KeyboardManager& keyboard_manager, std::filesystem::path config_directory,
    const CustomGlobalHotKey* initial,
    DiagnosticSink diagnostic_sink)
    : instance_(instance), owner_(owner), localization_(language),
      keyboard_manager_(keyboard_manager),
      config_directory_(std::move(config_directory)),
      diagnostic_sink_(std::move(diagnostic_sink)),
      initial_(initial ? std::optional<CustomGlobalHotKey>(*initial) : std::nullopt),
      gesture_(initial ? initial->binding.gesture : std::nullopt) {}

std::optional<CustomGlobalHotKey> CustomHotKeyDialog::run() {
    const WNDCLASSW window_class{
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &CustomHotKeyDialog::window_procedure,
        .hInstance = instance_,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = dialog_class_name,
    };
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return std::nullopt;
    }

    const auto system_dpi = GetDpiForSystem();
    window_ = CreateWindowExW(
        WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME, dialog_class_name,
        text(window_title_text), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(980, system_dpi, 96),
        MulDiv(700, system_dpi, 96), owner_, nullptr, instance_, this);
    if (!window_) return std::nullopt;
    settings_visual_style::apply_application_icons(window_, instance_, IDI_SIMPILOT);
    RECT rectangle{};
    GetWindowRect(window_, &rectangle);
    const auto monitor = MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_information{.cbSize = sizeof(monitor_information)};
    GetMonitorInfoW(monitor, &monitor_information);
    const auto width = rectangle.right - rectangle.left;
    const auto height = rectangle.bottom - rectangle.top;
    const auto x = monitor_information.rcWork.left
        + (monitor_information.rcWork.right - monitor_information.rcWork.left - width) / 2;
    const auto y = monitor_information.rcWork.top
        + (monitor_information.rcWork.bottom - monitor_information.rcWork.top - height) / 2;
    SetWindowPos(window_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    if (owner_) EnableWindow(owner_, FALSE);
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    MSG message{};
    while (IsWindow(window_) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (owner_) {
        EnableWindow(owner_, TRUE);
        SetForegroundWindow(owner_);
    }
    if (font_) DeleteObject(font_);
    if (section_font_) DeleteObject(section_font_);
    if (title_font_) DeleteObject(title_font_);
    font_ = nullptr;
    section_font_ = nullptr;
    title_font_ = nullptr;
    return result_;
}

void CustomHotKeyDialog::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    title_ = CreateWindowW(L"STATIC", text(title_text), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    trigger_heading_ = CreateWindowW(L"STATIC", text(trigger_heading_text), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    trigger_type_ = CreateWindowW(L"STATIC", text(trigger_type_text),
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    capture_button_ = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(capture_identifier)), instance_, nullptr);
    allow_modifiers_ = CreateWindowW(L"BUTTON", text(allow_modifiers_text),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(allow_modifiers_identifier)), instance_, nullptr);
    SendMessageW(allow_modifiers_, BM_SETCHECK, BST_CHECKED, 0);
    divider_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);

    action_heading_ = CreateWindowW(L"STATIC", text(action_heading_text), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    action_type_ = CreateWindowW(WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(action_type_identifier)), instance_, nullptr);
    add_combo_item(action_type_, text(open_application_text));
    add_combo_item(action_type_, text(open_folder_text));
    add_combo_item(action_type_, text(open_file_text));
    SendMessageW(action_type_, CB_SETMINVISIBLE, 3, 0);
    SendMessageW(action_type_, CB_SETCURSEL, 0, 0);

    program_label_ = CreateWindowW(L"STATIC", text(program_path_text), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    program_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(program_edit_identifier)), instance_, nullptr);
    program_browse_ = CreateWindowW(L"BUTTON", text(browse_text),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(program_browse_identifier)), instance_, nullptr);
    arguments_label_ = CreateWindowW(L"STATIC", text(arguments_text), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    arguments_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    working_directory_label_ = CreateWindowW(L"STATIC", text(working_directory_text),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    working_directory_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    working_directory_browse_ = CreateWindowW(L"BUTTON", text(browse_text),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(working_directory_browse_identifier)), instance_, nullptr);
    identity_label_ = CreateWindowW(L"STATIC", text(identity_text), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    identity_combo_ = CreateWindowW(WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    add_combo_item(identity_combo_, text(normal_identity_text));
    add_combo_item(identity_combo_, text(administrator_identity_text));
    SendMessageW(identity_combo_, CB_SETMINVISIBLE, 2, 0);
    SendMessageW(identity_combo_, CB_SETCURSEL, 0, 0);
    existing_label_ = CreateWindowW(L"STATIC", text(existing_process_text), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    existing_combo_ = CreateWindowW(WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    add_combo_item(existing_combo_, text(show_window_text));
    add_combo_item(existing_combo_, text(start_new_text));
    add_combo_item(existing_combo_, text(do_nothing_text));
    SendMessageW(existing_combo_, CB_SETMINVISIBLE, 3, 0);
    SendMessageW(existing_combo_, CB_SETCURSEL, 0, 0);
    visibility_label_ = CreateWindowW(L"STATIC", text(visibility_text), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    visibility_combo_ = CreateWindowW(WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    add_combo_item(visibility_combo_, text(normal_visibility_text));
    add_combo_item(visibility_combo_, text(minimized_text));
    add_combo_item(visibility_combo_, text(maximized_text));
    add_combo_item(visibility_combo_, text(hidden_text));
    SendMessageW(visibility_combo_, CB_SETMINVISIBLE, 4, 0);
    SendMessageW(visibility_combo_, CB_SETCURSEL, 0, 0);

    if (initial_) {
        SendMessageW(action_type_, CB_SETCURSEL,
            static_cast<WPARAM>(initial_->action), 0);
        SetWindowTextW(program_edit_, initial_->program_path.c_str());
        SetWindowTextW(arguments_edit_, initial_->arguments.c_str());
        SetWindowTextW(working_directory_edit_, initial_->working_directory.c_str());
        SendMessageW(identity_combo_, CB_SETCURSEL,
            initial_->run_as_administrator ? 1 : 0, 0);
        SendMessageW(existing_combo_, CB_SETCURSEL,
            static_cast<WPARAM>(initial_->existing_process_action), 0);
        SendMessageW(visibility_combo_, CB_SETCURSEL,
            static_cast<WPARAM>(initial_->visibility), 0);
    }

    save_button_ = CreateWindowW(L"BUTTON", text(save_text),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(save_identifier)), instance_, nullptr);
    cancel_button_ = CreateWindowW(L"BUTTON", text(cancel_text),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(cancel_identifier)), instance_, nullptr);
    update_capture_button();
    update_fonts();
    update_action_controls();
    update_save_state();
}

void CustomHotKeyDialog::update_fonts() {
    const auto old_font = font_;
    const auto old_section_font = section_font_;
    const auto old_title_font = title_font_;
    font_ = CreateFontW(-MulDiv(13, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    title_font_ = CreateFontW(-MulDiv(20, dpi_, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    section_font_ = CreateFontW(-MulDiv(16, dpi_, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    set_font(title_, title_font_);
    set_font(trigger_heading_, section_font_);
    set_font(action_heading_, section_font_);
    const std::array controls{
        trigger_type_, capture_button_, allow_modifiers_, action_type_, program_label_,
        program_edit_, program_browse_, arguments_label_, arguments_edit_,
        working_directory_label_, working_directory_edit_, working_directory_browse_,
        identity_label_, identity_combo_, existing_label_, existing_combo_, visibility_label_,
        visibility_combo_, save_button_, cancel_button_};
    for (const auto control : controls) set_font(control, font_);
    if (old_font) DeleteObject(old_font);
    if (old_section_font) DeleteObject(old_section_font);
    if (old_title_font) DeleteObject(old_title_font);
}

void CustomHotKeyDialog::layout_controls(const int width, const int height) {
    const auto scale = [this](const int value) { return MulDiv(value, dpi_, 96); };
    const auto margin = scale(24);
    const auto content_width = width - margin * 2;
    const auto control_height = scale(34);
    const auto browse_width = scale(80);
    const auto label_height = scale(24);
    const auto field_gap = scale(8);

    MoveWindow(title_, margin, scale(24), content_width, scale(38), TRUE);
    MoveWindow(trigger_heading_, margin, scale(82), content_width, scale(28), TRUE);
    MoveWindow(trigger_type_, margin, scale(116), content_width, scale(20), TRUE);
    MoveWindow(capture_button_, margin, scale(142), content_width, scale(42), TRUE);
    MoveWindow(allow_modifiers_, margin, scale(192), content_width, scale(28), TRUE);
    MoveWindow(divider_, margin, scale(232), content_width, scale(2), TRUE);

    MoveWindow(action_heading_, margin, scale(250), content_width, scale(28), TRUE);
    MoveWindow(action_type_, margin, scale(284), content_width, control_height, TRUE);
    auto place_edit = [&](const HWND label, const HWND edit, const HWND browse, int& y) {
        MoveWindow(label, margin, y, content_width, label_height, TRUE);
        y += scale(26);
        MoveWindow(edit, margin, y,
            browse ? content_width - browse_width - field_gap : content_width, control_height, TRUE);
        if (browse) MoveWindow(browse, margin + content_width - browse_width, y,
                               browse_width, control_height, TRUE);
        y += scale(48);
    };
    auto y = scale(334);
    place_edit(program_label_, program_edit_, program_browse_, y);
    place_edit(arguments_label_, arguments_edit_, nullptr, y);
    place_edit(working_directory_label_, working_directory_edit_, working_directory_browse_, y);

    const auto advanced_gap = scale(16);
    const auto advanced_width = (content_width - advanced_gap * 2) / 3;
    const auto advanced_y = scale(556);
    const auto place_combo = [&](const HWND label, const HWND combo, const int column) {
        const auto x = margin + column * (advanced_width + advanced_gap);
        MoveWindow(label, x, advanced_y, advanced_width, label_height, TRUE);
        MoveWindow(combo, x, advanced_y + scale(26), advanced_width, control_height, TRUE);
    };
    place_combo(identity_label_, identity_combo_, 0);
    place_combo(existing_label_, existing_combo_, 1);
    place_combo(visibility_label_, visibility_combo_, 2);

    MoveWindow(cancel_button_, width - margin - scale(96), height - scale(54),
               scale(96), scale(38), TRUE);
    MoveWindow(save_button_, width - margin - scale(202), height - scale(54),
               scale(96), scale(38), TRUE);
}

void CustomHotKeyDialog::begin_capture() {
    if (capturing_) return;
    if (!keyboard_manager_.begin_capture(
            [this](const KeyboardCaptureResult& result) {
                handle_capture_result(result);
            })) {
        diagnose_capture(L"activation failed");
        MessageBoxW(window_, text(capture_failed_text), text(window_title_text),
            MB_OK | MB_ICONERROR);
        return;
    }
    capturing_ = true;
    update_capture_button();
    update_save_state();
}

void CustomHotKeyDialog::cancel_capture() {
    diagnose_capture(L"cancelled");
    keyboard_manager_.end_capture();
    capturing_ = false;
    update_capture_button();
    update_save_state();
}

void CustomHotKeyDialog::complete_capture(const HotKeyGesture& gesture) {
    diagnose_capture(L"completed");
    keyboard_manager_.end_capture();
    capturing_ = false;
    if (!SendMessageW(allow_modifiers_, BM_GETCHECK, 0, 0) && gesture.modifiers != 0) {
        MessageBoxW(window_, text(modifiers_required_text), text(window_title_text),
            MB_OK | MB_ICONWARNING);
        update_capture_button();
        update_save_state();
        return;
    }
    const auto windows_index = windows_letter_hotkey_index(gesture);
    if (windows_index && *windows_index == static_cast<std::size_t>(L'L' - L'A')) {
        MessageBoxW(window_, text(win_l_unsupported_text), text(window_title_text),
            MB_OK | MB_ICONWARNING);
        update_capture_button();
        update_save_state();
        return;
    }
    gesture_ = gesture;
    update_capture_button();
    update_save_state();
}

void CustomHotKeyDialog::handle_capture_result(
    const KeyboardCaptureResult& result) {
    if (!capturing_) return;
    if (result.kind == KeyboardCaptureResultKind::cancelled) {
        cancel_capture();
    } else {
        complete_capture(result.gesture);
    }
}

void CustomHotKeyDialog::update_capture_button() {
    const auto label = hotkey_capture_button_text(gesture_, capturing_, localization_);
    SetWindowTextW(capture_button_, label.c_str());
}

void CustomHotKeyDialog::diagnose_capture(const std::wstring_view reason) const noexcept {
    if (!diagnostic_sink_) return;
    try {
        diagnostic_sink_(std::format(L"custom hotkey capture {} error={}",
                                     reason, keyboard_manager_.last_error()));
    } catch (...) {
    }
}

void CustomHotKeyDialog::update_action_controls() {
    const auto action = SendMessageW(action_type_, CB_GETCURSEL, 0, 0);
    const auto application = action == 0;
    SetWindowTextW(program_label_, text(application ? program_path_text
        : action == 1 ? folder_path_text : file_path_text));
    const std::array application_controls{
        arguments_label_, arguments_edit_, working_directory_label_, working_directory_edit_,
        working_directory_browse_, identity_label_, identity_combo_, existing_label_,
        existing_combo_, visibility_label_, visibility_combo_};
    for (const auto control : application_controls) {
        ShowWindow(control, application ? SW_SHOW : SW_HIDE);
    }
    update_save_state();
}

void CustomHotKeyDialog::browse_target() {
    if (SendMessageW(action_type_, CB_GETCURSEL, 0, 0) == 1) {
        BROWSEINFOW information{
            .hwndOwner = window_,
            .lpszTitle = text(folder_path_text),
            .ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE,
        };
        const auto item = SHBrowseForFolderW(&information);
        if (!item) return;
        std::array<wchar_t, MAX_PATH> path{};
        if (SHGetPathFromIDListW(item, path.data())) SetWindowTextW(program_edit_, path.data());
        CoTaskMemFree(item);
        return;
    }
    std::array<wchar_t, 32768> path{};
    const auto current = control_text(program_edit_);
    wcsncpy_s(path.data(), path.size(), current.c_str(), _TRUNCATE);
    const auto application = SendMessageW(action_type_, CB_GETCURSEL, 0, 0) == 0;
    std::wstring application_filter = text("file_dialog.applications");
    application_filter.push_back(L'\0');
    application_filter.append(L"*.exe");
    application_filter.push_back(L'\0');
    application_filter.append(text("file_dialog.all_files"));
    application_filter.push_back(L'\0');
    application_filter.append(L"*.*");
    application_filter.append(2, L'\0');
    std::wstring file_filter = text("file_dialog.all_files");
    file_filter.push_back(L'\0');
    file_filter.append(L"*.*");
    file_filter.append(2, L'\0');
    OPENFILENAMEW dialog{
        .lStructSize = sizeof(dialog),
        .hwndOwner = window_,
        .lpstrFilter = application ? application_filter.c_str() : file_filter.c_str(),
        .lpstrFile = path.data(),
        .nMaxFile = static_cast<DWORD>(path.size()),
        .Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR,
        .lpstrDefExt = application ? L"exe" : nullptr,
    };
    if (GetOpenFileNameW(&dialog)) SetWindowTextW(program_edit_, path.data());
}

void CustomHotKeyDialog::browse_working_directory() {
    BROWSEINFOW information{
        .hwndOwner = window_,
        .lpszTitle = text(working_directory_text),
        .ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE,
    };
    const auto item = SHBrowseForFolderW(&information);
    if (!item) return;
    std::array<wchar_t, MAX_PATH> path{};
    if (SHGetPathFromIDListW(item, path.data())) {
        SetWindowTextW(working_directory_edit_, path.data());
    }
    CoTaskMemFree(item);
}

void CustomHotKeyDialog::update_save_state() {
    const auto target = resolved_target();
    std::error_code error;
    const auto target_exists = SendMessageW(action_type_, CB_GETCURSEL, 0, 0) == 1
        ? std::filesystem::is_directory(target, error)
        : std::filesystem::is_regular_file(target, error);
    const auto enabled = gesture_.has_value() && !capturing_ && target_exists;
    EnableWindow(save_button_, enabled ? TRUE : FALSE);
}

std::filesystem::path CustomHotKeyDialog::resolved_target() const {
    const VariableExpander expander(config_directory_.wstring());
    auto target = std::filesystem::path(expander.expand(control_text(program_edit_)));
    if (target.is_relative()) target = config_directory_ / target;
    return target.lexically_normal();
}

void CustomHotKeyDialog::save() {
    if (!gesture_) return;
    CustomGlobalHotKey hotkey;
    hotkey.binding.gesture = gesture_;
    hotkey.binding.force_override = initial_ && initial_->binding.force_override;
    const auto action = SendMessageW(action_type_, CB_GETCURSEL, 0, 0);
    hotkey.action = action == 1 ? CustomHotKeyAction::open_folder
        : action == 2 ? CustomHotKeyAction::open_file
                      : CustomHotKeyAction::open_application;
    hotkey.program_path = control_text(program_edit_);
    std::error_code error;
    const auto target = resolved_target();
    const auto valid_target = hotkey.action == CustomHotKeyAction::open_folder
        ? std::filesystem::is_directory(target, error)
        : std::filesystem::is_regular_file(target, error);
    if (!valid_target) {
        MessageBoxW(window_, text(invalid_target_text), text(window_title_text),
                    MB_OK | MB_ICONWARNING);
        return;
    }
    if (hotkey.action == CustomHotKeyAction::open_application) {
        hotkey.arguments = control_text(arguments_edit_);
        hotkey.working_directory = control_text(working_directory_edit_);
        hotkey.run_as_administrator = SendMessageW(identity_combo_, CB_GETCURSEL, 0, 0) == 1;
        hotkey.existing_process_action = static_cast<ExistingProcessAction>(
            std::max<LRESULT>(0, SendMessageW(existing_combo_, CB_GETCURSEL, 0, 0)));
        hotkey.visibility = static_cast<LaunchVisibility>(
            std::max<LRESULT>(0, SendMessageW(visibility_combo_, CB_GETCURSEL, 0, 0)));
    }
    hotkey.enabled = !initial_ || initial_->enabled;
    result_ = std::move(hotkey);
    DestroyWindow(window_);
}

const wchar_t* CustomHotKeyDialog::text(const CustomHotKeyText identifier) const noexcept {
    const auto actual = initial_ && identifier == window_title_text ? edit_window_title_text
        : initial_ && identifier == title_text ? edit_title_text : identifier;
    return localization_.text(actual).data();
}

const wchar_t* CustomHotKeyDialog::text(const std::string_view key) const noexcept {
    return localization_.text(key).data();
}

LRESULT CALLBACK CustomHotKeyDialog::window_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* dialog = static_cast<CustomHotKeyDialog*>(creation->lpCreateParams);
        dialog->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
    }
    auto* dialog = reinterpret_cast<CustomHotKeyDialog*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    return dialog ? dialog->handle_message(message, wparam, lparam)
                  : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CustomHotKeyDialog::handle_message(
    const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_ERASEBKGND) {
        return settings_visual_style::erase_background(window_, wparam);
    }
    if (message == WM_CTLCOLORSTATIC
        && reinterpret_cast<HWND>(lparam) == trigger_type_) {
        return settings_visual_style::handle_secondary_text(wparam, lparam);
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
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        layout_controls(rectangle.right, rectangle.bottom);
        return 0;
    }
    if (message == WM_ACTIVATE && capturing_ && LOWORD(wparam) == WA_INACTIVE) {
        diagnose_capture(L"deactivated");
        return 0;
    }
    if (message == WM_COMMAND) {
        const auto identifier = LOWORD(wparam);
        if (identifier == capture_identifier && HIWORD(wparam) == BN_CLICKED) {
            begin_capture();
            return 0;
        }
        if (identifier == program_edit_identifier && HIWORD(wparam) == EN_CHANGE) {
            update_save_state();
            return 0;
        }
        if (identifier == action_type_identifier && HIWORD(wparam) == CBN_SELCHANGE) {
            update_action_controls();
            return 0;
        }
        if (identifier == program_browse_identifier && HIWORD(wparam) == BN_CLICKED) {
            browse_target();
            return 0;
        }
        if (identifier == working_directory_browse_identifier && HIWORD(wparam) == BN_CLICKED) {
            browse_working_directory();
            return 0;
        }
        if (identifier == save_identifier && HIWORD(wparam) == BN_CLICKED) {
            save();
            return 0;
        }
        if (identifier == cancel_identifier && HIWORD(wparam) == BN_CLICKED) {
            DestroyWindow(window_);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window_);
        return 0;
    }
    if (message == WM_DESTROY) {
        keyboard_manager_.end_capture();
        window_ = nullptr;
        return 0;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

} // namespace simpilot
