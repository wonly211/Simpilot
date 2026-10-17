#include "application_source_dialog.hpp"

#include "resource.h"
#include "settings_visual_style.hpp"

#include <algorithm>
#include <utility>

namespace simpilot {
namespace {

constexpr auto dialog_class_name = L"Simpilot.ApplicationSourceDialog";
constexpr int packaged_radio_identifier = 100;
constexpr int executable_radio_identifier = 101;
constexpr int apps_folder_radio_identifier = 102;
constexpr int dialog_client_width = 560;
constexpr int dialog_client_height = 384;

void set_font(const HWND control, const HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

} // namespace

std::optional<ApplicationSource> ApplicationSourceDialog::show_modal(
    const HINSTANCE instance, const HWND owner, std::string language_code) {
    ApplicationSourceDialog dialog(instance, owner, std::move(language_code));
    return dialog.run();
}

ApplicationSourceDialog::ApplicationSourceDialog(
    const HINSTANCE instance, const HWND owner, std::string language_code)
    : instance_(instance), owner_(owner), localization_(std::move(language_code)) {}

std::optional<ApplicationSource> ApplicationSourceDialog::run() {
    const WNDCLASSW window_class{
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &ApplicationSourceDialog::window_procedure,
        .hInstance = instance_,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = dialog_class_name,
    };
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return std::nullopt;
    }

    const auto system_dpi = GetDpiForSystem();
    constexpr auto dialog_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    constexpr auto dialog_extended_style = WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME;
    RECT initial_bounds{
        0, 0, MulDiv(dialog_client_width, system_dpi, 96),
        MulDiv(dialog_client_height, system_dpi, 96)};
    AdjustWindowRectExForDpi(
        &initial_bounds, dialog_style, FALSE, dialog_extended_style, system_dpi);
    window_ = CreateWindowExW(
        dialog_extended_style, dialog_class_name,
        text("menu_editor.choose_application_source"), dialog_style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        initial_bounds.right - initial_bounds.left,
        initial_bounds.bottom - initial_bounds.top,
        owner_, nullptr, instance_, this);
    if (!window_) return std::nullopt;
    settings_visual_style::apply_application_icons(window_, instance_, IDI_SIMPILOT);

    RECT rectangle{};
    GetWindowRect(window_, &rectangle);
    const auto monitor = MonitorFromWindow(
        owner_ ? owner_ : window_, MONITOR_DEFAULTTONEAREST);
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
    if (body_font_) DeleteObject(body_font_);
    body_font_ = nullptr;
    if (repost_quit) PostQuitMessage(quit_code);
    return result_;
}

void ApplicationSourceDialog::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    title_ = CreateWindowW(L"STATIC", text("menu_editor.choose_application_source"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    subtitle_ = CreateWindowW(L"STATIC", text("menu_editor.application_source.subtitle"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    packaged_radio_ = CreateWindowW(L"BUTTON",
        text("menu_editor.application_source.packaged"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(packaged_radio_identifier)), instance_, nullptr);
    packaged_description_ = CreateWindowW(
        L"STATIC", text("menu_editor.application_source.packaged_description"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    executable_radio_ = CreateWindowW(L"BUTTON", text("menu_editor.application_source.file"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(executable_radio_identifier)), instance_, nullptr);
    SendMessageW(executable_radio_, BM_SETCHECK, BST_CHECKED, 0);
    executable_description_ = CreateWindowW(
        L"STATIC", text("menu_editor.application_source.file_description"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    apps_folder_radio_ = CreateWindowW(L"BUTTON",
        text("menu_editor.application_source.apps_folder"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(apps_folder_radio_identifier)), instance_, nullptr);
    apps_folder_description_ = CreateWindowW(
        L"STATIC", text("menu_editor.application_source.apps_folder_description"),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    continue_button_ = CreateWindowW(L"BUTTON",
        text("menu_editor.application_source.continue"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDOK), instance_, nullptr);
    cancel_button_ = CreateWindowW(L"BUTTON", text("program_selection.cancel"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDCANCEL), instance_, nullptr);
    update_typography();
    RECT rectangle{};
    GetClientRect(window_, &rectangle);
    layout_controls(rectangle.right, rectangle.bottom);
    SetFocus(executable_radio_);
}

void ApplicationSourceDialog::update_typography() {
    const auto old_body = body_font_;
    body_font_ = CreateFontW(-MulDiv(14, dpi_, 96), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    typography_.update(body_font_, dpi_);
    set_font(title_, typography_.title());
    set_font(subtitle_, typography_.body());
    set_font(packaged_radio_, typography_.body());
    set_font(packaged_description_, typography_.caption());
    set_font(executable_radio_, typography_.body());
    set_font(executable_description_, typography_.caption());
    set_font(apps_folder_radio_, typography_.body());
    set_font(apps_folder_description_, typography_.caption());
    set_font(continue_button_, typography_.body());
    set_font(cancel_button_, typography_.body());
    settings_visual_style::style_button(
        continue_button_, settings_visual_style::ButtonStyle::primary);
    settings_visual_style::style_button(
        cancel_button_, settings_visual_style::ButtonStyle::quiet);
    if (old_body) DeleteObject(old_body);
}

void ApplicationSourceDialog::layout_controls(const int width, const int height) {
    const auto scale = [this](const int value) { return MulDiv(value, dpi_, 96); };
    const auto margin = scale(28);
    const auto content_width = std::max(0, width - margin * 2);
    const auto footer_top = height - scale(72);
    const auto option_x = margin + scale(24);
    const auto option_width = std::max(0, width - option_x - margin);

    MoveWindow(title_, margin, scale(28), content_width, scale(32), TRUE);
    MoveWindow(subtitle_, margin, scale(84), content_width, scale(20), TRUE);
    MoveWindow(packaged_radio_, margin, scale(120),
               content_width, scale(24), TRUE);
    MoveWindow(packaged_description_, option_x, scale(144),
               option_width, scale(18), TRUE);
    MoveWindow(executable_radio_, margin, scale(184),
               content_width, scale(24), TRUE);
    MoveWindow(executable_description_, option_x, scale(208),
               option_width, scale(18), TRUE);
    MoveWindow(apps_folder_radio_, margin, scale(248),
               content_width, scale(24), TRUE);
    MoveWindow(apps_folder_description_, option_x, scale(272),
               option_width, scale(18), TRUE);

    const auto button_y = footer_top + scale(18);
    MoveWindow(cancel_button_, margin, button_y, scale(88), scale(36), TRUE);
    MoveWindow(continue_button_, width - margin - scale(88), button_y,
               scale(88), scale(36), TRUE);
}

void ApplicationSourceDialog::accept_selection() {
    if (SendMessageW(packaged_radio_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        result_ = ApplicationSource::packaged;
    } else if (SendMessageW(apps_folder_radio_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        result_ = ApplicationSource::apps_folder;
    } else {
        result_ = ApplicationSource::executable;
    }
    DestroyWindow(window_);
}

const wchar_t* ApplicationSourceDialog::text(const std::string_view key) const noexcept {
    return localization_.text(key).data();
}

LRESULT CALLBACK ApplicationSourceDialog::window_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* dialog = static_cast<ApplicationSourceDialog*>(creation->lpCreateParams);
        dialog->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
    }
    auto* dialog = reinterpret_cast<ApplicationSourceDialog*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    return dialog ? dialog->handle_message(message, wparam, lparam)
                  : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT ApplicationSourceDialog::handle_message(
    const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_ERASEBKGND) {
        return settings_visual_style::erase_background(window_, wparam);
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT painting{};
        const auto dc = BeginPaint(window_, &painting);
        RECT client{};
        GetClientRect(window_, &client);
        FillRect(dc, &client, settings_visual_style::background_brush());
        settings_visual_style::draw_separator(
            dc, 0, client.right, client.bottom - MulDiv(72, dpi_, 96), dpi_);
        EndPaint(window_, &painting);
        return 0;
    }
    if (settings_visual_style::is_color_message(message)) {
        const auto control = reinterpret_cast<HWND>(lparam);
        if (message == WM_CTLCOLORSTATIC
            && (control == subtitle_ || control == packaged_description_
                || control == executable_description_
                || control == apps_folder_description_)) {
            return settings_visual_style::handle_secondary_text(wparam, lparam);
        }
        return settings_visual_style::handle_color_message(message, wparam, lparam);
    }
    if (message == WM_CREATE) {
        create_controls();
        return 0;
    }
    if (message == WM_DPICHANGED) {
        dpi_ = HIWORD(wparam);
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        update_typography();
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        layout_controls(rectangle.right, rectangle.bottom);
        InvalidateRect(window_, nullptr, TRUE);
        return 0;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == IDOK) {
            accept_selection();
            return 0;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            DestroyWindow(window_);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window_);
        return 0;
    }
    if (message == WM_DESTROY) {
        window_ = nullptr;
        return 0;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

} // namespace simpilot
