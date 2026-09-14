#include "keyboard_mapping_dialog.hpp"

#include "resource.h"
#include "settings_visual_style.hpp"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>

namespace simpilot {
namespace {

constexpr auto dialog_class_name = L"Simpilot.KeyboardMappingDialog";
constexpr int source_record_identifier = 100;
constexpr int target_record_identifier = 101;
constexpr int process_foreground_identifier = 102;
constexpr int exact_match_identifier = 103;
constexpr int enabled_identifier = 104;
constexpr int source_modifier_identifier = 110;
constexpr int source_action_identifier = 120;
constexpr int source_chord_identifier = 121;
constexpr int target_modifier_identifier = 130;
constexpr int target_action_identifier = 140;
constexpr int save_identifier = 1;
constexpr int cancel_identifier = 2;
constexpr auto no_option = std::numeric_limits<std::size_t>::max();

void set_font(const HWND control, const HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT,
                              reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring control_text(const HWND control) {
    const auto length = GetWindowTextLengthW(control);
    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(length));
    return result;
}

std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    const auto last = value.find_last_not_of(L" \t\r\n");
    return first == std::wstring::npos ? std::wstring{}
                                       : value.substr(first, last - first + 1);
}

bool combo_identifier(const int identifier) noexcept {
    return (identifier >= source_modifier_identifier
            && identifier < source_modifier_identifier + 4)
        || identifier == source_action_identifier
        || identifier == source_chord_identifier
        || (identifier >= target_modifier_identifier
            && identifier < target_modifier_identifier + 4)
        || identifier == target_action_identifier;
}

} // namespace

std::optional<KeyboardMappingRule> KeyboardMappingDialog::show_modal(
    const HINSTANCE instance, const HWND owner, std::string language_code,
    const KeyboardMappingRule* initial, CaptureCallbacks callbacks,
    DiagnosticSink diagnostic_sink) {
    KeyboardMappingDialog dialog(instance, owner, std::move(language_code),
                                 initial, std::move(callbacks),
                                 std::move(diagnostic_sink));
    return dialog.run();
}

KeyboardMappingDialog::KeyboardMappingDialog(
    const HINSTANCE instance, const HWND owner, std::string language_code,
    const KeyboardMappingRule* initial, CaptureCallbacks callbacks,
    DiagnosticSink diagnostic_sink)
    : instance_(instance), owner_(owner), localization_(std::move(language_code)),
      initial_(initial ? std::optional<KeyboardMappingRule>(*initial) : std::nullopt),
      editor_(initial ? KeyboardMappingEditorModel(*initial)
                      : KeyboardMappingEditorModel{}),
      callbacks_(std::move(callbacks)), diagnostic_sink_(std::move(diagnostic_sink)) {}

std::optional<KeyboardMappingRule> KeyboardMappingDialog::run() {
    const WNDCLASSW window_class{
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &KeyboardMappingDialog::window_procedure,
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
        text("settings.keyboard_mappings.dialog.title"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(960, system_dpi, 96),
        MulDiv(600, system_dpi, 96), owner_, nullptr, instance_, this);
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
    bool repost_quit = false;
    int quit_code = 0;
    MSG message{};
    while (IsWindow(window_)) {
        const auto message_result = GetMessageW(&message, nullptr, 0, 0);
        if (message_result <= 0) {
            if (message_result == 0) {
                repost_quit = true;
                quit_code = static_cast<int>(message.wParam);
            } else {
                diagnose(L"keyboard mapping dialog message loop failed");
            }
            break;
        }
        if (!IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (window_) DestroyWindow(window_);
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
    if (repost_quit) PostQuitMessage(quit_code);
    return result_;
}

void KeyboardMappingDialog::create_key_options() {
    modifier_options_.clear();
    source_action_options_.clear();
    action_options_.clear();
    for (const auto& key : keyboard_mapping_modifier_catalog()) {
        modifier_options_.push_back({.key = key});
    }
    for (const auto& key : keyboard_mapping_source_action_catalog()) {
        source_action_options_.push_back({.key = key});
    }
    for (const auto& key : keyboard_mapping_action_catalog()) {
        action_options_.push_back({.key = key});
    }
    ensure_model_options();
}

void KeyboardMappingDialog::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    create_key_options();
    const auto create_static = [this](const wchar_t* value, const DWORD style = 0) {
        return CreateWindowW(L"STATIC", value, WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    };
    const auto create_combo = [this](const int identifier) {
        return CreateWindowW(WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(identifier)), instance_, nullptr);
    };

    title_ = create_static(text("settings.keyboard_mappings.dialog.heading"));
    source_heading_ = create_static(text("settings.keyboard_mappings.source"));
    source_hint_ = create_static(text("settings.keyboard_mappings.source_hint"));
    source_summary_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    source_record_ = CreateWindowW(L"BUTTON",
        text("settings.keyboard_mappings.record_source"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(source_record_identifier)), instance_, nullptr);
    source_modifiers_label_ = create_static(
        text("settings.keyboard_mappings.modifiers"));
    for (std::size_t index = 0; index < source_modifier_combos_.size(); ++index) {
        source_modifier_combos_[index] = create_combo(
            source_modifier_identifier + static_cast<int>(index));
        populate_modifier_combo(source_modifier_combos_[index]);
    }
    source_action_label_ = create_static(
        text("settings.keyboard_mappings.primary_key"));
    source_action_combo_ = create_combo(source_action_identifier);
    populate_action_combo(source_action_combo_, false, source_action_options_);
    source_chord_label_ = create_static(
        text("settings.keyboard_mappings.chord_key"));
    source_chord_combo_ = create_combo(source_chord_identifier);
    populate_action_combo(source_chord_combo_, true, action_options_);

    target_heading_ = create_static(text("settings.keyboard_mappings.target"));
    target_hint_ = create_static(text("settings.keyboard_mappings.target_hint"));
    target_summary_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    target_record_ = CreateWindowW(L"BUTTON",
        text("settings.keyboard_mappings.record_target"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(target_record_identifier)), instance_, nullptr);
    target_modifiers_label_ = create_static(
        text("settings.keyboard_mappings.modifiers"));
    for (std::size_t index = 0; index < target_modifier_combos_.size(); ++index) {
        target_modifier_combos_[index] = create_combo(
            target_modifier_identifier + static_cast<int>(index));
        populate_modifier_combo(target_modifier_combos_[index]);
    }
    target_action_label_ = create_static(
        text("settings.keyboard_mappings.primary_key"));
    target_action_combo_ = create_combo(target_action_identifier);
    populate_action_combo(target_action_combo_, false, action_options_);

    divider_ = create_static(L"", SS_ETCHEDHORZ);
    purpose_label_ = create_static(text("settings.keyboard_mappings.purpose"),
                                   SS_CENTERIMAGE);
    purpose_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    process_label_ = create_static(text("settings.keyboard_mappings.process"),
                                   SS_CENTERIMAGE);
    process_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    process_foreground_ = CreateWindowW(L"BUTTON",
        text("settings.keyboard_mappings.use_foreground"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(process_foreground_identifier)), instance_, nullptr);
    exact_match_ = CreateWindowW(L"BUTTON",
        text("settings.keyboard_mappings.exact_match"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(exact_match_identifier)), instance_, nullptr);
    enabled_ = CreateWindowW(L"BUTTON", text("settings.keyboard_mappings.enabled"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(enabled_identifier)), instance_, nullptr);
    save_button_ = CreateWindowW(L"BUTTON", text("settings.save"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(save_identifier)), instance_, nullptr);
    cancel_button_ = CreateWindowW(L"BUTTON", text("settings.cancel"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(cancel_identifier)), instance_, nullptr);

    if (initial_) {
        SetWindowTextW(purpose_edit_, initial_->purpose.c_str());
        SetWindowTextW(process_edit_, initial_->process_name.c_str());
        SendMessageW(exact_match_, BM_SETCHECK,
                     initial_->exact_match ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(enabled_, BM_SETCHECK,
                     initial_->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    } else {
        SendMessageW(exact_match_, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(enabled_, BM_SETCHECK, BST_CHECKED, 0);
    }
    sync_controls_from_model();
    update_fonts();
}

void KeyboardMappingDialog::populate_modifier_combo(const HWND combo) {
    const auto empty = SendMessageW(combo, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(text("settings.keyboard_mappings.none")));
    SendMessageW(combo, CB_SETITEMDATA, empty, static_cast<LPARAM>(no_option));
    for (std::size_t index = 0; index < modifier_options_.size(); ++index) {
        const auto label = option_label(modifier_options_[index]);
        const auto row = SendMessageW(combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(combo, CB_SETITEMDATA, row, static_cast<LPARAM>(index));
    }
    SendMessageW(combo, CB_SETMINVISIBLE, 9, 0);
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void KeyboardMappingDialog::populate_action_combo(
    const HWND combo, const bool optional,
    const std::vector<KeyOption>& options) {
    const auto placeholder = text(optional
        ? "settings.keyboard_mappings.none"
        : "settings.keyboard_mappings.select_key");
    const auto empty = SendMessageW(combo, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(placeholder));
    SendMessageW(combo, CB_SETITEMDATA, empty, static_cast<LPARAM>(no_option));
    for (std::size_t index = 0; index < options.size(); ++index) {
        const auto label = option_label(options[index]);
        const auto row = SendMessageW(combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(combo, CB_SETITEMDATA, row, static_cast<LPARAM>(index));
    }
    SendMessageW(combo, CB_SETMINVISIBLE, 14, 0);
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void KeyboardMappingDialog::ensure_model_options() {
    for (const auto& key : editor_.source_modifiers()) {
        if (key) (void)ensure_modifier_option(*key, true);
    }
    for (const auto& key : editor_.target_modifiers()) {
        if (key) (void)ensure_modifier_option(*key, true);
    }
    if (editor_.source_action()) {
        (void)ensure_source_action_option(*editor_.source_action(), true);
    }
    if (editor_.source_chord_action()) {
        (void)ensure_action_option(*editor_.source_chord_action(), true);
    }
    if (editor_.target_action()) {
        (void)ensure_action_option(*editor_.target_action(), true);
    }
}

std::size_t KeyboardMappingDialog::ensure_source_action_option(
    const PhysicalKey& key, const bool recorded) {
    const auto found = std::ranges::find(
        source_action_options_, key, &KeyOption::key);
    if (found != source_action_options_.end()) {
        return static_cast<std::size_t>(
            found - source_action_options_.begin());
    }
    source_action_options_.push_back({.key = key, .recorded = recorded});
    const auto index = source_action_options_.size() - 1;
    if (source_action_combo_) append_source_action_option_to_control(index);
    return index;
}

std::size_t KeyboardMappingDialog::ensure_modifier_option(
    const PhysicalKey& key, const bool recorded) {
    const auto found = std::ranges::find(modifier_options_, key, &KeyOption::key);
    if (found != modifier_options_.end()) {
        return static_cast<std::size_t>(found - modifier_options_.begin());
    }
    modifier_options_.push_back({.key = key, .recorded = recorded});
    const auto index = modifier_options_.size() - 1;
    if (source_modifier_combos_.front()) append_modifier_option_to_controls(index);
    return index;
}

std::size_t KeyboardMappingDialog::ensure_action_option(
    const PhysicalKey& key, const bool recorded) {
    const auto found = std::ranges::find(action_options_, key, &KeyOption::key);
    if (found != action_options_.end()) {
        return static_cast<std::size_t>(found - action_options_.begin());
    }
    action_options_.push_back({.key = key, .recorded = recorded});
    const auto index = action_options_.size() - 1;
    if (source_action_combo_) append_action_option_to_controls(index);
    return index;
}

void KeyboardMappingDialog::append_modifier_option_to_controls(
    const std::size_t index) {
    const auto label = option_label(modifier_options_[index]);
    for (const auto combo : source_modifier_combos_) {
        const auto row = SendMessageW(combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(combo, CB_SETITEMDATA, row, static_cast<LPARAM>(index));
    }
    for (const auto combo : target_modifier_combos_) {
        const auto row = SendMessageW(combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(combo, CB_SETITEMDATA, row, static_cast<LPARAM>(index));
    }
}

void KeyboardMappingDialog::append_action_option_to_controls(
    const std::size_t index) {
    const auto label = option_label(action_options_[index]);
    for (const auto combo : {source_chord_combo_, target_action_combo_}) {
        const auto row = SendMessageW(combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(combo, CB_SETITEMDATA, row, static_cast<LPARAM>(index));
    }
}

void KeyboardMappingDialog::append_source_action_option_to_control(
    const std::size_t index) {
    const auto label = option_label(source_action_options_[index]);
    const auto row = SendMessageW(source_action_combo_, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(label.c_str()));
    SendMessageW(source_action_combo_, CB_SETITEMDATA, row,
                 static_cast<LPARAM>(index));
}

void KeyboardMappingDialog::select_combo_key(
    const HWND combo, const std::vector<KeyOption>& options,
    const std::optional<PhysicalKey>& key) {
    if (!key) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        return;
    }
    const auto found = std::ranges::find(options, *key, &KeyOption::key);
    if (found == options.end()) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        return;
    }
    const auto option = static_cast<std::size_t>(found - options.begin());
    const auto count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int row = 0; row < count; ++row) {
        if (static_cast<std::size_t>(SendMessageW(
                combo, CB_GETITEMDATA, row, 0)) == option) {
            SendMessageW(combo, CB_SETCURSEL, row, 0);
            return;
        }
    }
}

std::optional<PhysicalKey> KeyboardMappingDialog::combo_key(
    const HWND combo, const std::vector<KeyOption>& options) const noexcept {
    const auto row = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (row == CB_ERR) return std::nullopt;
    const auto data = static_cast<std::size_t>(
        SendMessageW(combo, CB_GETITEMDATA, row, 0));
    if (data == no_option || data >= options.size()) return std::nullopt;
    return options[data].key;
}

void KeyboardMappingDialog::sync_controls_from_model() {
    ensure_model_options();
    for (std::size_t index = 0; index < source_modifier_combos_.size(); ++index) {
        select_combo_key(source_modifier_combos_[index], modifier_options_,
                         editor_.source_modifiers()[index]);
        select_combo_key(target_modifier_combos_[index], modifier_options_,
                         editor_.target_modifiers()[index]);
    }
    select_combo_key(source_action_combo_, source_action_options_,
                     editor_.source_action());
    select_combo_key(source_chord_combo_, action_options_,
                     editor_.source_chord_action());
    select_combo_key(target_action_combo_, action_options_, editor_.target_action());
    update_previews();
    update_chord_availability();
}

void KeyboardMappingDialog::sync_model_from_controls() {
    for (std::size_t index = 0; index < source_modifier_combos_.size(); ++index) {
        editor_.set_source_modifier(index,
            combo_key(source_modifier_combos_[index], modifier_options_));
        editor_.set_target_modifier(index,
            combo_key(target_modifier_combos_[index], modifier_options_));
    }
    editor_.set_source_action(
        combo_key(source_action_combo_, source_action_options_));
    editor_.set_source_chord_action(combo_key(source_chord_combo_, action_options_));
    editor_.set_target_action(combo_key(target_action_combo_, action_options_));
}

void KeyboardMappingDialog::update_previews() {
    const auto source = source_preview();
    const auto target = target_preview();
    SetWindowTextW(source_summary_, source.c_str());
    SetWindowTextW(target_summary_, target.c_str());
}

void KeyboardMappingDialog::update_chord_availability() {
    const auto modifier_count = static_cast<std::size_t>(std::ranges::count_if(
        editor_.source_modifiers(), [](const auto& key) { return key.has_value(); }));
    const auto has_chord = editor_.source_chord_action().has_value();
    EnableWindow(source_chord_combo_, modifier_count < 4 ? TRUE : FALSE);
    for (std::size_t index = 0; index < source_modifier_combos_.size(); ++index) {
        const auto selected = editor_.source_modifiers()[index].has_value();
        EnableWindow(source_modifier_combos_[index],
            !has_chord || selected || modifier_count < 3 ? TRUE : FALSE);
    }
}

void KeyboardMappingDialog::handle_combo_change(const int identifier) {
    sync_model_from_controls();
    const auto draft = editor_.build();
    const auto source_duplicate =
        draft.error == KeyboardMappingDraftError::source_modifier_duplicate;
    const auto target_duplicate =
        draft.error == KeyboardMappingDraftError::target_modifier_duplicate;
    if (source_duplicate && identifier >= source_modifier_identifier
        && identifier < source_modifier_identifier + 4) {
        SendMessageW(source_modifier_combos_[identifier - source_modifier_identifier],
                     CB_SETCURSEL, 0, 0);
        sync_model_from_controls();
        show_draft_error(draft.error);
    } else if (target_duplicate && identifier >= target_modifier_identifier
               && identifier < target_modifier_identifier + 4) {
        SendMessageW(target_modifier_combos_[identifier - target_modifier_identifier],
                     CB_SETCURSEL, 0, 0);
        sync_model_from_controls();
        show_draft_error(draft.error);
    }
    update_previews();
    update_chord_availability();
}

void KeyboardMappingDialog::update_fonts() {
    const auto old_font = font_;
    const auto old_section_font = section_font_;
    const auto old_title_font = title_font_;
    font_ = CreateFontW(-MulDiv(14, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    section_font_ = CreateFontW(-MulDiv(15, dpi_, 96), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    title_font_ = CreateFontW(-MulDiv(22, dpi_, 96), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    set_font(title_, title_font_);
    set_font(source_heading_, section_font_);
    set_font(target_heading_, section_font_);
    const std::array controls{
        source_hint_, source_summary_, source_record_, source_modifiers_label_,
        source_action_label_, source_action_combo_, source_chord_label_,
        source_chord_combo_, target_hint_, target_summary_, target_record_,
        target_modifiers_label_, target_action_label_, target_action_combo_,
        purpose_label_, purpose_edit_, process_label_, process_edit_,
        process_foreground_, exact_match_,
        enabled_, save_button_, cancel_button_};
    for (const auto control : controls) set_font(control, font_);
    for (const auto control : source_modifier_combos_) set_font(control, font_);
    for (const auto control : target_modifier_combos_) set_font(control, font_);
    if (old_font) DeleteObject(old_font);
    if (old_section_font) DeleteObject(old_section_font);
    if (old_title_font) DeleteObject(old_title_font);
}

void KeyboardMappingDialog::layout_controls(const int width, const int height) {
    const auto scale = [this](const int value) { return MulDiv(value, dpi_, 96); };
    const auto margin = scale(24);
    const auto column_gap = scale(24);
    const auto content_width = std::max(1, width - margin * 2);
    const auto column_width = std::max(scale(300), (content_width - column_gap) / 2);
    const auto right_x = margin + column_width + column_gap;
    const auto row_height = scale(34);
    const auto record_width = scale(132);
    const auto summary_width = column_width - record_width - scale(8);
    const auto combo_gap = scale(8);
    const auto half_combo = (column_width - combo_gap) / 2;

    MoveWindow(title_, margin, scale(20), content_width, scale(36), TRUE);
    MoveWindow(source_heading_, margin, scale(68), column_width, scale(26), TRUE);
    MoveWindow(target_heading_, right_x, scale(68), column_width, scale(26), TRUE);
    MoveWindow(source_hint_, margin, scale(98), column_width, scale(40), TRUE);
    MoveWindow(target_hint_, right_x, scale(98), column_width, scale(40), TRUE);
    MoveWindow(source_summary_, margin, scale(144), summary_width, row_height, TRUE);
    MoveWindow(source_record_, margin + summary_width + scale(8), scale(144),
               record_width, row_height, TRUE);
    MoveWindow(target_summary_, right_x, scale(144), summary_width, row_height, TRUE);
    MoveWindow(target_record_, right_x + summary_width + scale(8), scale(144),
               record_width, row_height, TRUE);

    MoveWindow(source_modifiers_label_, margin, scale(188), column_width, scale(24), TRUE);
    MoveWindow(target_modifiers_label_, right_x, scale(188), column_width, scale(24), TRUE);
    for (std::size_t index = 0; index < 4; ++index) {
        const auto x = (index % 2 == 0) ? margin : margin + half_combo + combo_gap;
        const auto target_x = (index % 2 == 0) ? right_x
                                                : right_x + half_combo + combo_gap;
        const auto y = scale(index < 2 ? 214 : 254);
        MoveWindow(source_modifier_combos_[index], x, y, half_combo, scale(300), TRUE);
        MoveWindow(target_modifier_combos_[index], target_x, y,
                   half_combo, scale(300), TRUE);
    }

    MoveWindow(source_action_label_, margin, scale(300), half_combo, scale(24), TRUE);
    MoveWindow(source_chord_label_, margin + half_combo + combo_gap, scale(300),
               half_combo, scale(24), TRUE);
    MoveWindow(target_action_label_, right_x, scale(300), column_width, scale(24), TRUE);
    MoveWindow(source_action_combo_, margin, scale(326), half_combo, scale(360), TRUE);
    MoveWindow(source_chord_combo_, margin + half_combo + combo_gap, scale(326),
               half_combo, scale(360), TRUE);
    MoveWindow(target_action_combo_, right_x, scale(326), column_width, scale(360), TRUE);

    MoveWindow(divider_, margin, scale(378), content_width, scale(2), TRUE);
    const auto purpose_y = scale(400);
    const auto process_y = scale(444);
    const auto label_width = scale(104);
    const auto foreground_width = scale(170);
    MoveWindow(purpose_label_, margin, purpose_y, label_width, row_height, TRUE);
    MoveWindow(purpose_edit_, margin + label_width + scale(8), purpose_y,
               content_width - label_width - scale(8), row_height, TRUE);
    MoveWindow(process_label_, margin, process_y, label_width, row_height, TRUE);
    MoveWindow(process_edit_, margin + label_width + scale(8), process_y,
               content_width - label_width - foreground_width - scale(20),
               row_height, TRUE);
    MoveWindow(process_foreground_, width - margin - foreground_width, process_y,
               foreground_width, row_height, TRUE);
    MoveWindow(exact_match_, margin, process_y + scale(48), content_width / 2,
               row_height, TRUE);
    MoveWindow(enabled_, margin + content_width / 2, process_y + scale(48),
               content_width / 2, row_height, TRUE);
    const auto button_y = height - scale(56);
    MoveWindow(cancel_button_, width - margin - scale(100), button_y,
               scale(100), scale(34), TRUE);
    MoveWindow(save_button_, width - margin - scale(210), button_y,
               scale(100), scale(34), TRUE);
}

void KeyboardMappingDialog::begin_trigger_capture() {
    end_capture();
    if (!callbacks_.begin_trigger) {
        MessageBoxW(window_, text("settings.keyboard_mappings.capture_unavailable"),
                    text("settings.keyboard_mappings.dialog.title"),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    capturing_ = true;
    SetWindowTextW(source_record_, text("settings.keyboard_mappings.recording"));
    if (!callbacks_.begin_trigger([this](const std::optional<KeyboardTrigger> trigger) {
            if (window_ && trigger) {
                editor_.set_trigger(*trigger);
                sync_controls_from_model();
            }
            end_capture();
        })) {
        end_capture();
        MessageBoxW(window_, text("settings.keyboard_mappings.capture_unavailable"),
                    text("settings.keyboard_mappings.dialog.title"),
                    MB_OK | MB_ICONWARNING);
    }
}

void KeyboardMappingDialog::begin_output_capture() {
    end_capture();
    if (!callbacks_.begin_output) {
        MessageBoxW(window_, text("settings.keyboard_mappings.capture_unavailable"),
                    text("settings.keyboard_mappings.dialog.title"),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    capturing_ = true;
    SetWindowTextW(target_record_, text("settings.keyboard_mappings.recording"));
    if (!callbacks_.begin_output([this](const std::optional<KeyboardOutput> output) {
            if (window_ && output) {
                editor_.set_output(*output);
                sync_controls_from_model();
            }
            end_capture();
        })) {
        end_capture();
        MessageBoxW(window_, text("settings.keyboard_mappings.capture_unavailable"),
                    text("settings.keyboard_mappings.dialog.title"),
                    MB_OK | MB_ICONWARNING);
    }
}

void KeyboardMappingDialog::end_capture() {
    if (!capturing_) return;
    if (callbacks_.end) callbacks_.end();
    capturing_ = false;
    SetWindowTextW(source_record_, text("settings.keyboard_mappings.record_source"));
    SetWindowTextW(target_record_, text("settings.keyboard_mappings.record_target"));
}

void KeyboardMappingDialog::use_foreground_process() {
    std::optional<std::wstring> process_name;
    try {
        if (callbacks_.foreground_process) {
            process_name = callbacks_.foreground_process();
        }
    } catch (...) {
        diagnose(L"foreground process provider failed");
    }
    if (!process_name) {
        MessageBoxW(window_, text("settings.keyboard_mappings.process_unavailable"),
                    text("settings.keyboard_mappings.dialog.title"),
                    MB_OK | MB_ICONWARNING);
        return;
    }
    const auto name = normalize_mapping_process_name(std::move(*process_name));
    if (!name.empty()) SetWindowTextW(process_edit_, name.c_str());
}

void KeyboardMappingDialog::save() {
    end_capture();
    sync_model_from_controls();
    const auto draft = editor_.build();
    if (!draft) {
        show_draft_error(draft.error);
        return;
    }

    KeyboardMappingRule candidate;
    candidate.trigger = draft.trigger;
    candidate.output = draft.output;
    candidate.purpose = trim(control_text(purpose_edit_));
    const auto process = control_text(process_edit_);
    candidate.process_name = normalize_mapping_process_name(process);
    if (!trim(process).empty() && candidate.process_name.empty()) {
        MessageBoxW(window_, text("settings.keyboard_mappings.process_invalid"),
                    text("settings.keyboard_mappings.dialog.title"),
                    MB_OK | MB_ICONWARNING);
        return;
    }
    candidate.exact_match = SendMessageW(exact_match_, BM_GETCHECK, 0, 0)
        == BST_CHECKED;
    candidate.enabled = SendMessageW(enabled_, BM_GETCHECK, 0, 0)
        == BST_CHECKED;
    if (!validate_keyboard_mappings({candidate}).empty()) {
        MessageBoxW(window_, text("settings.keyboard_mappings.invalid"),
                    text("settings.keyboard_mappings.dialog.title"),
                    MB_OK | MB_ICONWARNING);
        return;
    }
    result_ = std::move(candidate);
    DestroyWindow(window_);
}

void KeyboardMappingDialog::show_draft_error(
    const KeyboardMappingDraftError error) const {
    std::string_view key = "settings.keyboard_mappings.invalid";
    switch (error) {
    case KeyboardMappingDraftError::source_action_required:
        key = "settings.keyboard_mappings.source_required";
        break;
    case KeyboardMappingDraftError::target_action_required:
        key = "settings.keyboard_mappings.target_required";
        break;
    case KeyboardMappingDraftError::source_modifier_duplicate:
    case KeyboardMappingDraftError::target_modifier_duplicate:
        key = "settings.keyboard_mappings.duplicate_modifier";
        break;
    case KeyboardMappingDraftError::source_chord_modifier_limit:
        key = "settings.keyboard_mappings.chord_modifier_limit";
        break;
    case KeyboardMappingDraftError::source_chord_invalid:
        key = "settings.keyboard_mappings.chord_invalid";
        break;
    case KeyboardMappingDraftError::source_modifier_action_requires_single:
        key = "settings.keyboard_mappings.modifier_source_must_be_single";
        break;
    default:
        break;
    }
    MessageBoxW(window_, text(key),
                text("settings.keyboard_mappings.dialog.title"),
                MB_OK | MB_ICONWARNING);
}

std::wstring KeyboardMappingDialog::key_label(const PhysicalKey& key) const {
    return localized_keyboard_mapping_key_label(key, localization_);
}

std::wstring KeyboardMappingDialog::option_label(const KeyOption& option) const {
    auto result = key_label(option.key);
    if (option.recorded) {
        result.append(text("settings.keyboard_mappings.recorded_suffix"));
    }
    return result;
}

std::wstring KeyboardMappingDialog::source_preview() const {
    std::wstring result;
    const auto append = [this, &result](const PhysicalKey& key) {
        if (!result.empty()) result.append(L" + ");
        result.append(key_label(key));
    };
    for (const auto& key : editor_.source_modifiers()) {
        if (key) append(*key);
    }
    if (editor_.source_action()) append(*editor_.source_action());
    if (editor_.source_chord_action()) append(*editor_.source_chord_action());
    return result.empty() ? text("settings.keyboard_mappings.not_selected") : result;
}

std::wstring KeyboardMappingDialog::target_preview() const {
    std::wstring result;
    const auto append = [this, &result](const PhysicalKey& key) {
        if (!result.empty()) result.append(L" + ");
        result.append(key_label(key));
    };
    for (const auto& key : editor_.target_modifiers()) {
        if (key) append(*key);
    }
    if (editor_.target_action()) append(*editor_.target_action());
    return result.empty() ? text("settings.keyboard_mappings.not_selected") : result;
}

const wchar_t* KeyboardMappingDialog::text(const std::string_view key) const noexcept {
    return localization_.text(key).data();
}

void KeyboardMappingDialog::diagnose(const std::wstring_view message) const noexcept {
    if (diagnostic_sink_) diagnostic_sink_(message);
}

LRESULT CALLBACK KeyboardMappingDialog::window_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* dialog = static_cast<KeyboardMappingDialog*>(creation->lpCreateParams);
        dialog->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto* dialog = reinterpret_cast<KeyboardMappingDialog*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    return dialog ? dialog->handle_message(message, wparam, lparam)
                  : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT KeyboardMappingDialog::handle_message(
    const UINT message, const WPARAM wparam, const LPARAM lparam) {
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
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        update_fonts();
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        layout_controls(rectangle.right, rectangle.bottom);
        return 0;
    }
    if (message == WM_COMMAND) {
        const auto identifier = LOWORD(wparam);
        const auto notification = HIWORD(wparam);
        if (notification == CBN_SELCHANGE && combo_identifier(identifier)) {
            handle_combo_change(identifier);
            return 0;
        }
        if (notification == BN_CLICKED && identifier == source_record_identifier) {
            begin_trigger_capture();
            return 0;
        }
        if (notification == BN_CLICKED && identifier == target_record_identifier) {
            begin_output_capture();
            return 0;
        }
        if (notification == BN_CLICKED && identifier == process_foreground_identifier) {
            use_foreground_process();
            return 0;
        }
        if (notification == BN_CLICKED && identifier == save_identifier) {
            save();
            return 0;
        }
        if (notification == BN_CLICKED && identifier == cancel_identifier) {
            end_capture();
            DestroyWindow(window_);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        end_capture();
        DestroyWindow(window_);
        return 0;
    }
    if (message == WM_DESTROY) {
        end_capture();
        window_ = nullptr;
        return 0;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

} // namespace simpilot
