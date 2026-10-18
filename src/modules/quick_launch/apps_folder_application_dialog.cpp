#include "apps_folder_application_dialog.hpp"

#include "resource.h"
#include "settings_visual_style.hpp"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>
#include <utility>

namespace simpilot {
namespace {

constexpr auto dialog_class_name = L"Simpilot.AppsFolderApplicationDialog";
constexpr int application_list_identifier = 100;

void set_font(const HWND control, const HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

} // namespace

std::optional<std::wstring> AppsFolderApplicationDialog::show_modal(
    const HINSTANCE instance, const HWND owner, std::string language_code,
    const std::vector<AppsFolderApplication>& applications,
    const AppsFolderApplicationScope scope, std::wstring current_target) {
    AppsFolderApplicationDialog dialog(
        instance, owner, std::move(language_code), applications, scope,
        std::move(current_target));
    return dialog.run();
}

AppsFolderApplicationDialog::AppsFolderApplicationDialog(
    const HINSTANCE instance, const HWND owner, std::string language_code,
    const std::vector<AppsFolderApplication>& applications,
    const AppsFolderApplicationScope scope, std::wstring current_target)
    : instance_(instance), owner_(owner), localization_(std::move(language_code)),
      applications_(applications), scope_(scope),
      current_target_(std::move(current_target)) {}

std::optional<std::wstring> AppsFolderApplicationDialog::run() {
    INITCOMMONCONTROLSEX common_controls{
        .dwSize = sizeof(common_controls),
        .dwICC = ICC_LISTVIEW_CLASSES,
    };
    if (!InitCommonControlsEx(&common_controls)) return std::nullopt;

    const WNDCLASSW window_class{
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &AppsFolderApplicationDialog::window_procedure,
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
        text(scope_ == AppsFolderApplicationScope::packaged
            ? "apps_folder_application.window_title.packaged"
            : "apps_folder_application.window_title.all"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(980, system_dpi, 96),
        MulDiv(540, system_dpi, 96), owner_, nullptr, instance_, this);
    if (!window_) return std::nullopt;
    settings_visual_style::apply_application_icons(window_, instance_, IDI_SIMPILOT);

    RECT rectangle{};
    GetWindowRect(window_, &rectangle);
    const auto monitor = MonitorFromWindow(owner_ ? owner_ : window_, MONITOR_DEFAULTTONEAREST);
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
    font_ = nullptr;
    if (repost_quit) PostQuitMessage(quit_code);
    return result_;
}

void AppsFolderApplicationDialog::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    prompt_ = CreateWindowW(L"STATIC", text(
        scope_ == AppsFolderApplicationScope::packaged
            ? "apps_folder_application.prompt.packaged"
            : "apps_folder_application.prompt.all"),
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    list_ = CreateWindowExW(WS_EX_STATICEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(application_list_identifier)), instance_, nullptr);
    ListView_SetExtendedListViewStyle(
        list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    settings_visual_style::style_list_view(list_);

    LVCOLUMNW column{.mask = LVCF_TEXT | LVCF_WIDTH};
    column.pszText = const_cast<wchar_t*>(text("apps_folder_application.name"));
    column.cx = 260;
    ListView_InsertColumn(list_, 0, &column);
    column.pszText = const_cast<wchar_t*>(text("apps_folder_application.package"));
    column.cx = 300;
    ListView_InsertColumn(list_, 1, &column);
    column.pszText = const_cast<wchar_t*>(text("apps_folder_application.aumid"));
    column.cx = 440;
    ListView_InsertColumn(list_, 2, &column);

    const auto current = lowercase(current_target_);
    int selected_row = -1;
    for (std::size_t index = 0; index < applications_.size(); ++index) {
        const auto& application = applications_[index];
        const auto target = AppsFolderApplicationCatalog::shell_target(application.aumid);
        LVITEMW item{
            .mask = LVIF_TEXT | LVIF_PARAM,
            .iItem = static_cast<int>(index),
            .pszText = const_cast<wchar_t*>(application.display_name.data()),
            .lParam = static_cast<LPARAM>(index),
        };
        const auto row = ListView_InsertItem(list_, &item);
        const auto package_text = application.package_family_name.empty()
            ? text("apps_folder_application.desktop")
            : application.package_family_name.c_str();
        ListView_SetItemText(list_, row, 1,
            const_cast<wchar_t*>(package_text));
        ListView_SetItemText(list_, row, 2,
            const_cast<wchar_t*>(application.aumid.data()));
        if (!current.empty() && lowercase(target) == current) selected_row = row;
    }
    if (selected_row < 0 && !applications_.empty()) selected_row = 0;
    if (selected_row >= 0) {
        ListView_SetItemState(list_, selected_row, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }

    select_button_ = CreateWindowW(L"BUTTON", text("program_selection.select"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDOK), instance_, nullptr);
    cancel_button_ = CreateWindowW(L"BUTTON", text("program_selection.cancel"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDCANCEL), instance_, nullptr);
    update_font();
    SetFocus(list_);
}

void AppsFolderApplicationDialog::update_font() {
    const auto old_font = font_;
    font_ = CreateFontW(-MulDiv(14, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    set_font(prompt_, font_);
    set_font(list_, font_);
    set_font(select_button_, font_);
    set_font(cancel_button_, font_);
    settings_visual_style::style_button(select_button_, settings_visual_style::ButtonStyle::primary);
    settings_visual_style::style_button(cancel_button_, settings_visual_style::ButtonStyle::quiet);
    if (old_font) DeleteObject(old_font);
}

void AppsFolderApplicationDialog::layout_controls(const int width, const int height) {
    const auto scale = [this](const int value) { return MulDiv(value, dpi_, 96); };
    const auto margin = scale(28);
    const auto button_width = scale(88);
    const auto button_height = scale(36);
    const auto gap = scale(8);
    const auto button_y = height - scale(54);
    MoveWindow(prompt_, margin, scale(18), width - margin * 2, scale(36), TRUE);
    MoveWindow(list_, margin, scale(62), width - margin * 2,
               std::max(0, button_y - scale(76)), TRUE);
    MoveWindow(select_button_, width - margin - button_width, button_y,
               button_width, button_height, TRUE);
    MoveWindow(cancel_button_, width - margin - button_width * 2 - gap, button_y,
               button_width, button_height, TRUE);

    const auto list_width = std::max(0, width - margin * 2 - GetSystemMetrics(SM_CXVSCROLL) - scale(4));
    ListView_SetColumnWidth(list_, 0, std::max(scale(220), list_width * 26 / 100));
    ListView_SetColumnWidth(list_, 1, std::max(scale(240), list_width * 30 / 100));
    ListView_SetColumnWidth(list_, 2, std::max(scale(360), list_width * 44 / 100));
}

void AppsFolderApplicationDialog::accept_selection() {
    const auto row = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
    if (row < 0) return;
    LVITEMW item{.mask = LVIF_PARAM, .iItem = row};
    if (!ListView_GetItem(list_, &item)) return;
    const auto index = static_cast<std::size_t>(item.lParam);
    if (index >= applications_.size()) return;
    result_ = applications_[index].aumid;
    DestroyWindow(window_);
}

const wchar_t* AppsFolderApplicationDialog::text(const std::string_view key) const noexcept {
    return localization_.text(key).data();
}

LRESULT CALLBACK AppsFolderApplicationDialog::window_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* dialog = static_cast<AppsFolderApplicationDialog*>(creation->lpCreateParams);
        dialog->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
    }
    auto* dialog = reinterpret_cast<AppsFolderApplicationDialog*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    return dialog ? dialog->handle_message(message, wparam, lparam)
                  : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT AppsFolderApplicationDialog::handle_message(
    const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_ERASEBKGND) {
        return settings_visual_style::erase_background(window_, wparam);
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
        update_font();
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        layout_controls(rectangle.right, rectangle.bottom);
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize.x = MulDiv(760, dpi_, 96);
        limits->ptMinTrackSize.y = MulDiv(400, dpi_, 96);
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
    if (message == WM_NOTIFY) {
        const auto* notification = reinterpret_cast<const NMHDR*>(lparam);
        if (notification->idFrom == application_list_identifier
            && notification->code == NM_DBLCLK) {
            accept_selection();
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
