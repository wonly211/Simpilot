#include "keyboard_mapping_dialog.hpp"

#include "resource.h"
#include "settings_visual_style.hpp"

#include <algorithm>
#include <cwchar>
#include <format>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace simpilot {
namespace {

constexpr auto dialog_class_name = L"Simpilot.KeyboardMappingDialog";
constexpr int source_record_identifier = 100;
constexpr int target_record_identifier = 101;
constexpr int process_foreground_identifier = 102;
constexpr int exact_match_identifier = 103;
constexpr int enabled_identifier = 104;
constexpr int save_identifier = 1;
constexpr int cancel_identifier = 2;

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

std::vector<std::wstring> split(const std::wstring_view value,
                                const wchar_t separator) {
    std::vector<std::wstring> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(separator, start);
        result.emplace_back(value.substr(start, end - start));
        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    return result;
}

std::optional<unsigned long> unsigned_value(const std::wstring_view value) {
    const auto normalized = trim(std::wstring(value));
    if (normalized.empty()) return std::nullopt;
    wchar_t* end = nullptr;
    const auto parsed = std::wcstoul(normalized.c_str(), &end, 10);
    if (end == normalized.c_str() || *end != L'\0') return std::nullopt;
    return parsed;
}

std::optional<PhysicalKey> parse_key(const std::wstring_view value) {
    const auto fields = split(value, L':');
    if (fields.size() != 3) return std::nullopt;
    const auto virtual_key = unsigned_value(fields[0]);
    const auto scan_code = unsigned_value(fields[1]);
    const auto extended = unsigned_value(fields[2]);
    if (!virtual_key.has_value() || !scan_code.has_value()
        || !extended.has_value() || *extended > 1
        || *virtual_key > 0xFFFFUL || *scan_code > 0xFFFFUL) {
        return std::nullopt;
    }
    return PhysicalKey{static_cast<UINT>(*virtual_key),
                       static_cast<UINT>(*scan_code), *extended != 0};
}

std::optional<KeyboardTrigger> parse_trigger(const std::wstring& value) {
    const auto normalized = trim(value);
    if (normalized.empty()) return std::nullopt;
    const auto shortcut = split(normalized, L'=');
    if (shortcut.size() == 1) {
        const auto action = parse_key(shortcut.front());
        if (!action) return std::nullopt;
        KeyboardTrigger result;
        result.single_key = true;
        result.action = *action;
        return result;
    }
    // The editor format uses "mod|mod=>action[=>chord]".  Splitting on
    // '=' leaves an empty field around the arrow, which is intentional.
    if (shortcut.size() != 3 && shortcut.size() != 5
        || shortcut[1] != L">") {
        return std::nullopt;
    }
    KeyboardTrigger result;
    if (!shortcut[0].empty()) {
        const auto modifiers = split(shortcut[0], L'|');
        if (modifiers.empty() || modifiers.size() > 4) return std::nullopt;
        result.modifier_count = modifiers.size();
        for (std::size_t index = 0; index < modifiers.size(); ++index) {
            const auto modifier = parse_key(modifiers[index]);
            if (!modifier) return std::nullopt;
            result.modifiers[index] = *modifier;
        }
    } else if (shortcut.size() != 5) {
        // Only a two-action chord may omit source modifiers.
        return std::nullopt;
    }
    const auto action = parse_key(shortcut[2]);
    if (!action) return std::nullopt;
    result.action = *action;
    if (shortcut.size() == 5) {
        const auto chord = parse_key(shortcut[4]);
        if (!chord) return std::nullopt;
        result.chord_action = *chord;
    }
    return result;
}

std::optional<KeyboardOutput> parse_output(const std::wstring& value) {
    const auto normalized = trim(value);
    if (normalized.empty()) return std::nullopt;
    const auto shortcut = split(normalized, L'=');
    if (shortcut.size() == 1) {
        const auto action = parse_key(shortcut.front());
        if (!action) return std::nullopt;
        KeyboardOutput result;
        result.single_key = true;
        result.action = *action;
        return result;
    }
    if (shortcut.size() != 3 || shortcut[1] != L">") return std::nullopt;
    const auto modifiers = split(shortcut[0], L'|');
    if (modifiers.empty() || modifiers.size() > 4) return std::nullopt;
    KeyboardOutput result;
    result.modifier_count = modifiers.size();
    for (std::size_t index = 0; index < modifiers.size(); ++index) {
        const auto modifier = parse_key(modifiers[index]);
        if (!modifier) return std::nullopt;
        result.modifiers[index] = *modifier;
    }
    const auto action = parse_key(shortcut[2]);
    if (!action) return std::nullopt;
    result.action = *action;
    return result;
}

std::wstring key_token(const PhysicalKey& key) {
    return std::to_wstring(key.virtual_key) + L":"
        + std::to_wstring(key.scan_code) + L":"
        + (key.extended ? L"1" : L"0");
}

std::wstring trigger_text(const KeyboardTrigger& trigger) {
    if (trigger.single_key) return key_token(trigger.action);
    std::wstring result;
    for (std::size_t index = 0; index < trigger.modifier_count; ++index) {
        if (!result.empty()) result.push_back(L'|');
        result.append(key_token(trigger.modifiers[index]));
    }
    result.append(L"=>").append(key_token(trigger.action));
    if (trigger.chord_action) {
        result.append(L"=>").append(key_token(*trigger.chord_action));
    }
    return result;
}

std::wstring output_text(const KeyboardOutput& output) {
    if (output.single_key) return key_token(output.action);
    std::wstring result;
    for (std::size_t index = 0; index < output.modifier_count; ++index) {
        if (!result.empty()) result.push_back(L'|');
        result.append(key_token(output.modifiers[index]));
    }
    result.append(L"=>").append(key_token(output.action));
    return result;
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
        CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(900, system_dpi, 96),
        MulDiv(610, system_dpi, 96), owner_, nullptr, instance_, this);
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

void KeyboardMappingDialog::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    title_ = CreateWindowW(L"STATIC", text("settings.keyboard_mappings.dialog.heading"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    source_heading_ = CreateWindowW(L"STATIC", text("settings.keyboard_mappings.source"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    source_hint_ = CreateWindowW(L"STATIC", text("settings.keyboard_mappings.source_hint"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    source_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    source_record_ = CreateWindowW(L"BUTTON",
        text("settings.keyboard_mappings.record_source"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(source_record_identifier)), instance_, nullptr);
    target_heading_ = CreateWindowW(L"STATIC", text("settings.keyboard_mappings.target"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    target_hint_ = CreateWindowW(L"STATIC", text("settings.keyboard_mappings.target_hint"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    target_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    target_record_ = CreateWindowW(L"BUTTON",
        text("settings.keyboard_mappings.record_target"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(target_record_identifier)), instance_, nullptr);
    process_label_ = CreateWindowW(L"STATIC", text("settings.keyboard_mappings.process"),
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    process_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    process_foreground_ = CreateWindowW(L"BUTTON",
        text("settings.keyboard_mappings.use_foreground"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(process_foreground_identifier)), instance_, nullptr);
    exact_match_ = CreateWindowW(L"BUTTON", text("settings.keyboard_mappings.exact_match"),
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
        SetWindowTextW(source_edit_, trigger_text(initial_->trigger).c_str());
        SetWindowTextW(target_edit_, output_text(initial_->output).c_str());
        SetWindowTextW(process_edit_, initial_->process_name.c_str());
        SendMessageW(exact_match_, BM_SETCHECK,
                     initial_->exact_match ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(enabled_, BM_SETCHECK,
                     initial_->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    } else {
        SendMessageW(exact_match_, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(enabled_, BM_SETCHECK, BST_CHECKED, 0);
    }
    update_fonts();
}

void KeyboardMappingDialog::update_fonts() {
    const auto old_font = font_;
    const auto old_section_font = section_font_;
    const auto old_title_font = title_font_;
    font_ = CreateFontW(-MulDiv(15, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    section_font_ = CreateFontW(-MulDiv(15, dpi_, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                                FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    title_font_ = CreateFontW(-MulDiv(22, dpi_, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                              FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    set_font(title_, title_font_);
    set_font(source_heading_, section_font_);
    set_font(target_heading_, section_font_);
    for (const auto control : {source_hint_, target_hint_, process_label_, source_edit_,
                               target_edit_, process_edit_, source_record_, target_record_,
                               process_foreground_, exact_match_, enabled_, save_button_,
                               cancel_button_}) {
        set_font(control, font_);
    }
    if (old_font) DeleteObject(old_font);
    if (old_section_font) DeleteObject(old_section_font);
    if (old_title_font) DeleteObject(old_title_font);
}

void KeyboardMappingDialog::layout_controls(const int width, const int height) {
    const auto scale = [this](const int value) { return MulDiv(value, dpi_, 96); };
    const auto margin = scale(28);
    const auto content_width = std::max(1, width - margin * 2);
    const auto heading_y = scale(24);
    const auto hint_y = scale(62);
    const auto row_height = scale(38);
    const auto button_width = scale(150);
    const auto field_width = std::max(scale(220), content_width - button_width - scale(12));
    MoveWindow(title_, margin, heading_y, content_width, scale(32), TRUE);
    MoveWindow(source_heading_, margin, hint_y, content_width, scale(26), TRUE);
    MoveWindow(source_hint_, margin, hint_y + scale(30), content_width, scale(38), TRUE);
    MoveWindow(source_edit_, margin, hint_y + scale(76), field_width, row_height, TRUE);
    MoveWindow(source_record_, margin + field_width + scale(12), hint_y + scale(76),
               button_width, row_height, TRUE);
    MoveWindow(target_heading_, margin, hint_y + scale(132), content_width, scale(26), TRUE);
    MoveWindow(target_hint_, margin, hint_y + scale(162), content_width, scale(38), TRUE);
    MoveWindow(target_edit_, margin, hint_y + scale(208), field_width, row_height, TRUE);
    MoveWindow(target_record_, margin + field_width + scale(12), hint_y + scale(208),
               button_width, row_height, TRUE);
    const auto process_y = hint_y + scale(270);
    const auto label_width = scale(120);
    const auto foreground_width = scale(190);
    MoveWindow(process_label_, margin, process_y, label_width, row_height, TRUE);
    MoveWindow(process_edit_, margin + label_width + scale(8), process_y,
               content_width - label_width - foreground_width - scale(20), row_height, TRUE);
    MoveWindow(process_foreground_, width - margin - foreground_width, process_y,
               foreground_width, row_height, TRUE);
    MoveWindow(exact_match_, margin, process_y + scale(54), content_width / 2, row_height, TRUE);
    MoveWindow(enabled_, margin + content_width / 2, process_y + scale(54),
               content_width / 2, row_height, TRUE);
    const auto button_y = height - scale(58);
    MoveWindow(cancel_button_, width - margin - scale(100), button_y, scale(100), scale(36), TRUE);
    MoveWindow(save_button_, width - margin - scale(212), button_y, scale(100), scale(36), TRUE);
}

void KeyboardMappingDialog::update_source_text() {
    if (capturing_) SetWindowTextW(source_record_, text("settings.keyboard_mappings.recording"));
}

void KeyboardMappingDialog::update_target_text() {
    if (capturing_) SetWindowTextW(target_record_, text("settings.keyboard_mappings.recording"));
}

void KeyboardMappingDialog::end_capture() {
    if (!capturing_) return;
    if (callbacks_.end) callbacks_.end();
    capturing_ = false;
    capturing_output_ = false;
    SetWindowTextW(source_record_, text("settings.keyboard_mappings.record_source"));
    SetWindowTextW(target_record_, text("settings.keyboard_mappings.record_target"));
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
    capturing_output_ = false;
    SetWindowTextW(source_record_, text("settings.keyboard_mappings.recording"));
    if (!callbacks_.begin_trigger([this](const std::optional<KeyboardTrigger> trigger) {
            if (window_ && trigger) {
                SetWindowTextW(source_edit_, trigger_text(*trigger).c_str());
            }
            end_capture();
        })) {
        end_capture();
        MessageBoxW(window_, text("settings.keyboard_mappings.capture_unavailable"),
                    text("settings.keyboard_mappings.dialog.title"), MB_OK | MB_ICONWARNING);
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
    capturing_output_ = true;
    SetWindowTextW(target_record_, text("settings.keyboard_mappings.recording"));
    if (!callbacks_.begin_output([this](const std::optional<KeyboardOutput> output) {
            if (window_ && output) {
                SetWindowTextW(target_edit_, output_text(*output).c_str());
            }
            end_capture();
        })) {
        end_capture();
        MessageBoxW(window_, text("settings.keyboard_mappings.capture_unavailable"),
                    text("settings.keyboard_mappings.dialog.title"), MB_OK | MB_ICONWARNING);
    }
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
                    text("settings.keyboard_mappings.dialog.title"), MB_OK | MB_ICONWARNING);
        return;
    }
    const auto name = normalize_mapping_process_name(std::move(*process_name));
    if (name.empty()) return;
    SetWindowTextW(process_edit_, name.c_str());
}

std::optional<KeyboardMappingRule> KeyboardMappingDialog::read_rule() const {
    const auto trigger = parse_trigger(control_text(source_edit_));
    const auto output = parse_output(control_text(target_edit_));
    if (!trigger || !output) return std::nullopt;
    KeyboardMappingRule result;
    result.trigger = *trigger;
    result.output = *output;
    const auto process = control_text(process_edit_);
    result.process_name = normalize_mapping_process_name(process);
    if (!trim(process).empty() && result.process_name.empty()) return std::nullopt;
    result.exact_match = SendMessageW(exact_match_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    result.enabled = SendMessageW(enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return result;
}

void KeyboardMappingDialog::save() {
    end_capture();
    const auto candidate = read_rule();
    if (!candidate) {
        MessageBoxW(window_, text("settings.keyboard_mappings.invalid_format"),
                    text("settings.keyboard_mappings.dialog.title"), MB_OK | MB_ICONWARNING);
        return;
    }
    const auto errors = validate_keyboard_mappings({*candidate});
    if (!errors.empty()) {
        MessageBoxW(window_, text("settings.keyboard_mappings.invalid"),
                    text("settings.keyboard_mappings.dialog.title"), MB_OK | MB_ICONWARNING);
        return;
    }
    result_ = *candidate;
    DestroyWindow(window_);
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
