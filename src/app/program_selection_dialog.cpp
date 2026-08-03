#include "program_selection_dialog.hpp"

#include "resource.h"
#include "settings_visual_style.hpp"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <ctime>
#include <format>
#include <iterator>
#include <string>
#include <utility>

namespace simpilot {
namespace {

constexpr auto dialog_class_name = L"Simpilot.ProgramSelectionDialog";
constexpr int candidate_list_identifier = 100;

void set_font(const HWND control, const HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring version_text(const std::uint64_t version) {
    if (version == 0) return L"-";
    const auto high = static_cast<std::uint32_t>(version >> 32U);
    const auto low = static_cast<std::uint32_t>(version);
    return std::format(L"{}.{}.{}.{}", HIWORD(high), LOWORD(high),
                       HIWORD(low), LOWORD(low));
}

std::wstring time_text(const std::filesystem::file_time_type value) {
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        value - std::filesystem::file_time_type::clock::now()
        + std::chrono::system_clock::now());
    const auto raw_time = std::chrono::system_clock::to_time_t(system_time);
    std::tm local_time{};
    if (localtime_s(&local_time, &raw_time) != 0) return L"-";
    wchar_t buffer[32]{};
    return std::wcsftime(buffer, std::size(buffer), L"%Y-%m-%d %H:%M", &local_time) > 0
        ? std::wstring(buffer) : std::wstring(L"-");
}

} // namespace

std::optional<std::filesystem::path> ProgramSelectionDialog::show_modal(
    const HINSTANCE instance, const HWND owner, const UiLanguage language,
    std::wstring executable, const std::vector<ProgramCandidate>& candidates) {
    ProgramSelectionDialog dialog(
        instance, owner, language, std::move(executable), candidates);
    return dialog.run();
}

ProgramSelectionDialog::ProgramSelectionDialog(
    const HINSTANCE instance, const HWND owner, const UiLanguage language,
    std::wstring executable, const std::vector<ProgramCandidate>& candidates)
    : instance_(instance), owner_(owner), localization_(language),
      executable_(std::move(executable)), candidates_(candidates) {}

std::optional<std::filesystem::path> ProgramSelectionDialog::run() {
    INITCOMMONCONTROLSEX common_controls{
        .dwSize = sizeof(common_controls),
        .dwICC = ICC_LISTVIEW_CLASSES,
    };
    if (!InitCommonControlsEx(&common_controls)) return std::nullopt;

    const WNDCLASSW window_class{
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &ProgramSelectionDialog::window_procedure,
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
        text("program_selection.window_title"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(900, system_dpi, 96),
        MulDiv(520, system_dpi, 96), owner_, nullptr, instance_, this);
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
    font_ = nullptr;
    return result_;
}

void ProgramSelectionDialog::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    const auto prompt = std::vformat(
        localization_.text("program_selection.prompt"),
        std::make_wformat_args(executable_));
    prompt_ = CreateWindowW(L"STATIC", prompt.c_str(), WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    list_ = CreateWindowExW(WS_EX_STATICEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(candidate_list_identifier)), instance_, nullptr);
    ListView_SetExtendedListViewStyle(
        list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    settings_visual_style::style_list_view(list_);

    LVCOLUMNW column{.mask = LVCF_TEXT | LVCF_WIDTH};
    column.pszText = const_cast<wchar_t*>(text("program_selection.file_path"));
    column.cx = 520;
    ListView_InsertColumn(list_, 0, &column);
    column.pszText = const_cast<wchar_t*>(text("program_selection.version"));
    column.cx = 120;
    ListView_InsertColumn(list_, 1, &column);
    column.pszText = const_cast<wchar_t*>(text("program_selection.modified"));
    column.cx = 160;
    ListView_InsertColumn(list_, 2, &column);

    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        auto path = candidates_[index].path.wstring();
        SHFILEINFOW file_information{};
        const auto system_image_list = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
            path.c_str(), 0, &file_information, sizeof(file_information),
            SHGFI_SYSICONINDEX | SHGFI_LARGEICON));
        if (system_image_list && !ListView_GetImageList(list_, LVSIL_SMALL)) {
            ListView_SetImageList(list_, system_image_list, LVSIL_SMALL);
        }
        LVITEMW item{
            .mask = static_cast<UINT>(LVIF_TEXT | LVIF_PARAM
                | (system_image_list ? LVIF_IMAGE : 0)),
            .iItem = static_cast<int>(index),
            .pszText = path.data(),
            .iImage = file_information.iIcon,
            .lParam = static_cast<LPARAM>(index),
        };
        const auto row = ListView_InsertItem(list_, &item);
        auto version = version_text(candidates_[index].version);
        auto modified = time_text(candidates_[index].last_write_time);
        ListView_SetItemText(list_, row, 1, version.data());
        ListView_SetItemText(list_, row, 2, modified.data());
    }
    if (!candidates_.empty()) {
        ListView_SetItemState(list_, 0, LVIS_SELECTED | LVIS_FOCUSED,
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

void ProgramSelectionDialog::update_font() {
    const auto old_font = font_;
    font_ = CreateFontW(-MulDiv(13, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    set_font(prompt_, font_);
    set_font(list_, font_);
    set_font(select_button_, font_);
    set_font(cancel_button_, font_);
    if (old_font) DeleteObject(old_font);
}

void ProgramSelectionDialog::layout_controls(const int width, const int height) {
    const auto scale = [this](const int value) { return MulDiv(value, dpi_, 96); };
    const auto margin = scale(24);
    const auto button_width = scale(96);
    const auto button_height = scale(36);
    const auto gap = scale(12);
    const auto button_y = height - margin - button_height;
    MoveWindow(prompt_, margin, scale(18), width - margin * 2, scale(36), TRUE);
    MoveWindow(list_, margin, scale(62), width - margin * 2,
               std::max(0, button_y - scale(76)), TRUE);
    MoveWindow(cancel_button_, width - margin - button_width, button_y,
               button_width, button_height, TRUE);
    MoveWindow(select_button_, width - margin - button_width * 2 - gap, button_y,
               button_width, button_height, TRUE);

    const auto list_width = std::max(0, width - margin * 2 - GetSystemMetrics(SM_CXVSCROLL) - scale(4));
    ListView_SetColumnWidth(list_, 0, list_width * 64 / 100);
    ListView_SetColumnWidth(list_, 1, list_width * 15 / 100);
    ListView_SetColumnWidth(list_, 2, list_width - list_width * 79 / 100);
}

void ProgramSelectionDialog::accept_selection() {
    const auto row = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
    if (row < 0) return;
    LVITEMW item{.mask = LVIF_PARAM, .iItem = row};
    if (!ListView_GetItem(list_, &item)) return;
    const auto index = static_cast<std::size_t>(item.lParam);
    if (index >= candidates_.size()) return;
    result_ = candidates_[index].path;
    DestroyWindow(window_);
}

const wchar_t* ProgramSelectionDialog::text(const std::string_view key) const noexcept {
    return localization_.text(key).data();
}

LRESULT CALLBACK ProgramSelectionDialog::window_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* dialog = static_cast<ProgramSelectionDialog*>(creation->lpCreateParams);
        dialog->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
    }
    auto* dialog = reinterpret_cast<ProgramSelectionDialog*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    return dialog ? dialog->handle_message(message, wparam, lparam)
                  : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT ProgramSelectionDialog::handle_message(
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
        limits->ptMinTrackSize.x = MulDiv(680, dpi_, 96);
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
        if (notification->idFrom == candidate_list_identifier
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
