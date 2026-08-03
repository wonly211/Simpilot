#include "about_window.hpp"

#include "resource.h"
#include "settings_visual_style.hpp"

#include <commctrl.h>
#include <shellscalingapi.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

namespace simpilot {
namespace {

constexpr int home_identifier = 100;
constexpr int releases_identifier = 101;
constexpr int manual_identifier = 102;
constexpr int issues_identifier = 103;
constexpr int license_identifier = 104;
constexpr int third_party_identifier = 105;

constexpr auto project_home_url = L"https://github.com/wonly211/Simpilot";
constexpr auto releases_url = L"https://github.com/wonly211/Simpilot/releases/latest";
constexpr auto manual_url =
    L"https://github.com/wonly211/Simpilot/blob/main/docs/user-manual.zh-CN.md";
constexpr auto issues_url = L"https://github.com/wonly211/Simpilot/issues";
constexpr auto license_url = L"https://github.com/wonly211/Simpilot/blob/main/LICENSE";
constexpr auto third_party_url =
    L"https://github.com/wonly211/Simpilot/blob/main/THIRD-PARTY-NOTICES.txt";

void set_font(const HWND control, const HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring link_markup(const std::wstring_view label) {
    std::wstring result = L"<a>";
    for (const auto character : label) {
        switch (character) {
        case L'&': result.append(L"&amp;"); break;
        case L'<': result.append(L"&lt;"); break;
        case L'>': result.append(L"&gt;"); break;
        default: result.push_back(character); break;
        }
    }
    result.append(L"</a>");
    return result;
}

SIZE about_window_size(const UINT dpi) noexcept {
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL;
    RECT desired{0, 0, MulDiv(680, dpi, 96), MulDiv(420, dpi, 96)};
    AdjustWindowRectExForDpi(&desired, style,
                            FALSE, WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME, dpi);
    return SIZE{desired.right - desired.left, desired.bottom - desired.top};
}

UINT monitor_dpi(const HMONITOR monitor) noexcept {
    UINT horizontal = 96;
    UINT vertical = 96;
    if (monitor && SUCCEEDED(GetDpiForMonitor(
            monitor, MDT_EFFECTIVE_DPI, &horizontal, &vertical))) {
        return horizontal;
    }
    return GetDpiForSystem();
}

struct TextSize final {
    int width;
    int height;
};

TextSize measure_text(const HWND window, const HFONT font, const wchar_t* value,
                      const int maximum_width, const UINT formatting) noexcept {
    const auto dc = GetDC(window);
    if (!dc) return {};
    const auto previous = SelectObject(dc, font);
    RECT rectangle{0, 0, std::max(1, maximum_width), 0};
    DrawTextW(dc, value, -1, &rectangle,
              DT_CALCRECT | DT_NOPREFIX | formatting);
    SelectObject(dc, previous);
    ReleaseDC(window, dc);
    return TextSize{static_cast<int>(rectangle.right - rectangle.left),
                    static_cast<int>(rectangle.bottom - rectangle.top)};
}

std::wstring window_text(const HWND window) {
    const auto length = GetWindowTextLengthW(window);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

} // namespace

void AboutWindow::show_modal(const HINSTANCE instance, const HWND owner,
                             const Localization& localization,
                             std::filesystem::path executable_path,
                             std::wstring version) {
    AboutWindow window(instance, owner, localization, std::move(executable_path),
                       std::move(version));
    window.run();
}

AboutWindow::AboutWindow(const HINSTANCE instance, const HWND owner,
                         const Localization& localization,
                         std::filesystem::path executable_path, std::wstring version)
    : instance_(instance), owner_(owner), localization_(localization),
      executable_path_(std::move(executable_path)), version_(std::move(version)) {}

void AboutWindow::run() {
    INITCOMMONCONTROLSEX common_controls{
        .dwSize = sizeof(common_controls),
        .dwICC = ICC_STANDARD_CLASSES | ICC_LINK_CLASS,
    };
    InitCommonControlsEx(&common_controls);

    const WNDCLASSW window_class{
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &AboutWindow::window_procedure,
        .hInstance = instance_,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .lpszClassName = about_window_class_name,
    };
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

    POINT cursor{};
    auto monitor = GetCursorPos(&cursor)
        ? MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST) : nullptr;
    if (!monitor && owner_) {
        monitor = MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST);
    }
    if (!monitor) {
        monitor = MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO monitor_information{.cbSize = sizeof(monitor_information)};
    GetMonitorInfoW(monitor, &monitor_information);
    const auto initial_size = about_window_size(monitor_dpi(monitor));
    const auto work_width = monitor_information.rcWork.right - monitor_information.rcWork.left;
    const auto work_height = monitor_information.rcWork.bottom - monitor_information.rcWork.top;
    const auto width = std::min(initial_size.cx, work_width * 96 / 100);
    const auto height = std::min(initial_size.cy, work_height * 96 / 100);
    const auto x = monitor_information.rcWork.left + (work_width - width) / 2;
    const auto y = monitor_information.rcWork.top + (work_height - height) / 2;

    window_ = CreateWindowExW(
        WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME, about_window_class_name,
        localization_.text(UiText::about).data(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_VSCROLL,
        x, y, width, height, owner_, nullptr, instance_, this);
    if (!window_) return;
    const auto actual_size = about_window_size(GetDpiForWindow(window_));
    const auto actual_width = std::min(actual_size.cx, work_width * 96 / 100);
    const auto actual_height = std::min(actual_size.cy, work_height * 96 / 100);
    SetWindowPos(window_, nullptr,
        monitor_information.rcWork.left + (work_width - actual_width) / 2,
        monitor_information.rcWork.top + (work_height - actual_height) / 2,
        actual_width, actual_height, SWP_NOZORDER | SWP_NOACTIVATE);
    settings_visual_style::apply_application_icons(window_, instance_, IDI_SIMPILOT);

    const auto disable_owner = owner_ && IsWindowEnabled(owner_);
    if (disable_owner) EnableWindow(owner_, FALSE);
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    SetFocus(close_button_);

    bool repost_quit = false;
    int quit_code = 0;
    MSG message{};
    while (IsWindow(window_)) {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) {
                repost_quit = true;
                quit_code = static_cast<int>(message.wParam);
            }
            break;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            DestroyWindow(window_);
            continue;
        }
        if (!IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (window_) DestroyWindow(window_);
    if (disable_owner && IsWindow(owner_)) {
        EnableWindow(owner_, TRUE);
        if (IsWindowVisible(owner_)) SetForegroundWindow(owner_);
    }
    if (font_) DeleteObject(font_);
    if (semibold_font_) DeleteObject(semibold_font_);
    if (section_font_) DeleteObject(section_font_);
    if (title_font_) DeleteObject(title_font_);
    font_ = nullptr;
    semibold_font_ = nullptr;
    section_font_ = nullptr;
    title_font_ = nullptr;
    if (repost_quit) PostQuitMessage(quit_code);
}

void AboutWindow::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    icon_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ICON,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    const auto icon_size = MulDiv(60, dpi_, 96);
    const auto icon = LoadImageW(instance_, MAKEINTRESOURCEW(IDI_SIMPILOT), IMAGE_ICON,
                                 icon_size, icon_size, LR_SHARED);
    SendMessageW(icon_, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);

    product_name_ = CreateWindowW(L"STATIC", localization_.text(UiText::app_title).data(),
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    positioning_ = CreateWindowW(L"STATIC", text(AboutText::positioning_text),
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    const auto version = std::vformat(text(AboutText::version_text),
                                     std::make_wformat_args(version_));
    version_label_ = CreateWindowW(L"STATIC", version.c_str(),
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    tagline_ = CreateWindowW(L"STATIC", text(AboutText::tagline_text),
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);

    const std::array primary_texts{
        AboutText::home_link_text, AboutText::releases_link_text,
        AboutText::manual_link_text, AboutText::issues_link_text};
    for (std::size_t index = 0; index < primary_links_.size(); ++index) {
        const auto markup = link_markup(text(primary_texts[index]));
        primary_links_[index] = CreateWindowW(WC_LINK, markup.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LWS_TRANSPARENT | LWS_NOPREFIX,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(home_identifier + index)), instance_, nullptr);
    }

    product_information_ = CreateWindowW(L"STATIC",
        text(AboutText::product_information_text),
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    const std::array label_texts{
        AboutText::system_label_text, AboutText::distribution_label_text,
        AboutText::license_label_text};
    const std::array value_texts{
        AboutText::system_value_text, AboutText::distribution_value_text,
        AboutText::license_value_text};
    for (std::size_t index = 0; index < information_labels_.size(); ++index) {
        information_labels_[index] = CreateWindowW(L"STATIC", text(label_texts[index]),
            WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0,
            window_, nullptr, instance_, nullptr);
        information_values_[index] = CreateWindowW(L"STATIC", text(value_texts[index]),
            WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0,
            window_, nullptr, instance_, nullptr);
    }
    local_data_ = CreateWindowW(L"STATIC", text(AboutText::local_data_text),
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    third_party_summary_ = CreateWindowW(L"STATIC", text(AboutText::third_party_summary_text),
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);

    const std::array legal_texts{
        AboutText::license_link_text, AboutText::third_party_link_text};
    for (std::size_t index = 0; index < legal_links_.size(); ++index) {
        const auto markup = link_markup(text(legal_texts[index]));
        legal_links_[index] = CreateWindowW(WC_LINK, markup.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LWS_TRANSPARENT | LWS_NOPREFIX,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(license_identifier + index)), instance_, nullptr);
    }
    close_button_ = CreateWindowW(L"BUTTON", text(AboutText::close_text),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
        instance_, nullptr);
    update_fonts();
}

void AboutWindow::update_fonts() {
    const auto old_font = font_;
    const auto old_semibold = semibold_font_;
    const auto old_section = section_font_;
    const auto old_title = title_font_;
    font_ = CreateFontW(-MulDiv(13, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    semibold_font_ = CreateFontW(-MulDiv(13, dpi_, 96), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    section_font_ = CreateFontW(-MulDiv(16, dpi_, 96), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    title_font_ = CreateFontW(-MulDiv(20, dpi_, 96), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    set_font(product_name_, title_font_);
    set_font(product_information_, section_font_);
    for (const auto control : information_labels_) set_font(control, semibold_font_);
    const std::array regular_controls{
        positioning_, version_label_, tagline_, local_data_, third_party_summary_,
        close_button_};
    for (const auto control : regular_controls) set_font(control, font_);
    for (const auto control : primary_links_) set_font(control, font_);
    for (const auto control : information_values_) set_font(control, font_);
    for (const auto control : legal_links_) set_font(control, font_);
    if (old_font) DeleteObject(old_font);
    if (old_semibold) DeleteObject(old_semibold);
    if (old_section) DeleteObject(old_section);
    if (old_title) DeleteObject(old_title);
}

void AboutWindow::layout_controls(const int width, const int height) {
    const auto scale = [this](const int value) { return MulDiv(value, dpi_, 96); };
    const auto margin = scale(28);
    const auto content_width = std::max(scale(200), width - margin * 2);
    struct Placement final {
        HWND control;
        int x;
        int y;
        int width;
        int height;
    };
    std::vector<Placement> placements;
    const auto place = [&placements](const HWND control, const int x, const int y,
                                     const int control_width,
                                     const int control_height) {
        placements.push_back({control, x, y, std::max(1, control_width),
                              std::max(1, control_height)});
    };
    const auto paragraph_height = [this, &scale](
            const wchar_t* value, const HFONT font, const int maximum_width) {
        return std::max(scale(18), measure_text(
            window_, font, value, maximum_width, DT_WORDBREAK).height);
    };

    auto y = scale(16);
    const auto icon_size = scale(60);
    place(icon_, margin, y, icon_size, icon_size);
    const auto header_x = margin + icon_size + scale(20);
    const auto header_width = std::max(scale(100), width - margin - header_x);
    auto header_y = y - scale(3);
    const auto product_height = paragraph_height(
        localization_.text(UiText::app_title).data(), title_font_, header_width);
    place(product_name_, header_x, header_y, header_width, product_height);
    header_y += product_height;
    const auto positioning_height = paragraph_height(
        text(AboutText::positioning_text), font_, header_width);
    place(positioning_, header_x, header_y, header_width, positioning_height);
    header_y += positioning_height + scale(1);
    const auto version_value = window_text(version_label_);
    const auto version_height = paragraph_height(
        version_value.c_str(), font_, header_width);
    place(version_label_, header_x, header_y, header_width, version_height);
    const auto header_bottom = std::max(y + icon_size, header_y + version_height);
    divider_positions_[0] = header_bottom + scale(10);

    y = divider_positions_[0] + scale(10);
    const auto tagline_height = paragraph_height(
        text(AboutText::tagline_text), font_, content_width);
    place(tagline_, margin, y, content_width, tagline_height);
    y += tagline_height + scale(7);

    const auto place_link_flow = [this, &placements, &scale](
            const auto& controls, const auto& identifiers, const int left,
            const int top, const int available_width) {
        auto x = left;
        auto row_y = top;
        auto row_height = 0;
        for (std::size_t index = 0; index < controls.size(); ++index) {
            auto size = measure_text(window_, font_, text(identifiers[index]),
                                     available_width, DT_SINGLELINE);
            const auto natural_width = size.width + scale(8);
            const auto control_width = std::min(available_width, natural_width);
            auto control_height = std::max(scale(22), size.height + scale(4));
            if (x != left && x + control_width > left + available_width) {
                x = left;
                row_y += row_height + scale(4);
                row_height = 0;
            }
            if (natural_width > available_width) {
                size = measure_text(window_, font_, text(identifiers[index]),
                                    available_width, DT_WORDBREAK);
                control_height = std::max(control_height, size.height + scale(4));
            }
            placements.push_back({controls[index], x, row_y, control_width,
                                  control_height});
            x += control_width + scale(12);
            row_height = std::max(row_height, control_height);
        }
        return row_y + row_height;
    };

    const std::array primary_texts{
        AboutText::home_link_text, AboutText::releases_link_text,
        AboutText::manual_link_text, AboutText::issues_link_text};
    y = place_link_flow(primary_links_, primary_texts, margin, y, content_width)
        + scale(9);

    const auto section_height = paragraph_height(
        text(AboutText::product_information_text), section_font_, content_width);
    place(product_information_, margin, y, content_width, section_height);
    y += section_height + scale(7);

    const std::array label_texts{
        AboutText::system_label_text, AboutText::distribution_label_text,
        AboutText::license_label_text};
    const std::array value_texts{
        AboutText::system_value_text, AboutText::distribution_value_text,
        AboutText::license_value_text};
    auto label_width = scale(112);
    for (const auto identifier : label_texts) {
        label_width = std::max(label_width, measure_text(
            window_, semibold_font_, text(identifier), content_width,
            DT_SINGLELINE).width + scale(6));
    }
    label_width = std::min(label_width, content_width / 3);
    const auto value_x = margin + label_width + scale(12);
    const auto value_width = std::max(
        scale(80), content_width - label_width - scale(12));
    for (std::size_t index = 0; index < information_labels_.size(); ++index) {
        const auto label_height = paragraph_height(
            text(label_texts[index]), semibold_font_, label_width);
        const auto value_height = paragraph_height(
            text(value_texts[index]), font_, value_width);
        const auto row_height = std::max(label_height, value_height);
        place(information_labels_[index], margin, y, label_width, row_height);
        place(information_values_[index], value_x, y, value_width, row_height);
        y += row_height + scale(2);
    }
    y += scale(4);
    const auto local_data_height = paragraph_height(
        text(AboutText::local_data_text), font_, content_width);
    place(local_data_, margin, y, content_width, local_data_height);
    y += local_data_height + scale(7);
    const auto third_party_height = paragraph_height(
        text(AboutText::third_party_summary_text), font_, content_width);
    place(third_party_summary_, margin, y, content_width, third_party_height);
    y += third_party_height + scale(10);

    divider_positions_[1] = y;
    y += scale(10);
    const auto button_width = scale(96);
    const auto button_height = scale(34);
    const auto button_x = width - margin - button_width;
    const auto legal_width = std::max(
        scale(120), content_width - button_width - scale(20));
    const std::array legal_texts{
        AboutText::license_link_text, AboutText::third_party_link_text};
    const auto legal_bottom = place_link_flow(
        legal_links_, legal_texts, margin, y, legal_width);
    place(close_button_, button_x, y, button_width, button_height);
    content_height_ = std::max(legal_bottom, y + button_height) + scale(16);

    SCROLLINFO scrolling{
        .cbSize = sizeof(scrolling),
        .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
        .nMin = 0,
        .nMax = std::max(0, content_height_ - 1),
        .nPage = static_cast<UINT>(std::max(0, height)),
        .nPos = scroll_position_,
    };
    SetScrollInfo(window_, SB_VERT, &scrolling, TRUE);
    scrolling.fMask = SIF_POS;
    GetScrollInfo(window_, SB_VERT, &scrolling);
    scroll_position_ = scrolling.nPos;

    for (const auto& placement : placements) {
        MoveWindow(placement.control, placement.x,
                   placement.y - scroll_position_, placement.width,
                   placement.height, TRUE);
    }
}

void AboutWindow::scroll_to(const int position) {
    SCROLLINFO scrolling{.cbSize = sizeof(scrolling), .fMask = SIF_ALL};
    if (!GetScrollInfo(window_, SB_VERT, &scrolling)) return;
    const auto maximum = std::max(
        scrolling.nMin,
        scrolling.nMax - static_cast<int>(scrolling.nPage) + 1);
    const auto next = std::clamp(position, scrolling.nMin, maximum);
    if (next == scroll_position_) return;
    scroll_position_ = next;
    RECT client{};
    GetClientRect(window_, &client);
    layout_controls(client.right, client.bottom);
    InvalidateRect(window_, nullptr, TRUE);
}

void AboutWindow::open_target(const int identifier) {
    std::wstring target;
    switch (identifier) {
    case home_identifier: target = project_home_url; break;
    case releases_identifier: target = releases_url; break;
    case manual_identifier: target = manual_url; break;
    case issues_identifier: target = issues_url; break;
    case license_identifier: {
        const auto local = executable_path_.parent_path() / L"LICENSE";
        std::error_code error;
        target = std::filesystem::exists(local, error) ? local.wstring() : license_url;
        break;
    }
    case third_party_identifier: {
        const auto local = executable_path_.parent_path() / L"THIRD-PARTY-NOTICES.txt";
        std::error_code error;
        target = std::filesystem::exists(local, error) ? local.wstring() : third_party_url;
        break;
    }
    default: return;
    }
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        window_, L"open", target.c_str(), nullptr, executable_path_.parent_path().c_str(),
        SW_SHOWNORMAL));
    if (result <= 32) {
        MessageBoxW(window_, text(AboutText::open_failed_text),
                    localization_.text(UiText::about).data(), MB_OK | MB_ICONWARNING);
    }
}

const wchar_t* AboutWindow::text(const AboutText identifier) const noexcept {
    return localization_.text(identifier).data();
}

LRESULT CALLBACK AboutWindow::window_procedure(const HWND window, const UINT message,
                                                const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* dialog = static_cast<AboutWindow*>(creation->lpCreateParams);
        dialog->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(dialog));
    }
    auto* dialog = reinterpret_cast<AboutWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    return dialog ? dialog->handle_message(window, message, wparam, lparam)
                  : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT AboutWindow::handle_message(const HWND window, const UINT message,
                                    const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_CREATE) {
        create_controls();
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        layout_controls(rectangle.right, rectangle.bottom);
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return settings_visual_style::erase_background(window_, wparam);
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
        const auto control = reinterpret_cast<HWND>(lparam);
        if (control == positioning_ || control == version_label_ || control == local_data_
            || control == third_party_summary_) {
            return settings_visual_style::handle_secondary_text(wparam, lparam);
        }
        return settings_visual_style::handle_color_message(message, wparam, lparam);
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT painting{};
        const auto dc = BeginPaint(window_, &painting);
        const auto scale = [this](const int value) { return MulDiv(value, dpi_, 96); };
        RECT client{};
        GetClientRect(window_, &client);
        const auto color = settings_visual_style::high_contrast_enabled()
            ? GetSysColor(COLOR_3DSHADOW) : RGB(213, 217, 221);
        const auto pen = CreatePen(PS_SOLID, 1, color);
        const auto previous = SelectObject(dc, pen);
        const auto margin = scale(28);
        for (const auto content_y : divider_positions_) {
            const auto y = content_y - scroll_position_;
            if (y < 0 || y > client.bottom) continue;
            MoveToEx(dc, margin, y, nullptr);
            LineTo(dc, client.right - margin, y);
        }
        SelectObject(dc, previous);
        DeleteObject(pen);
        EndPaint(window_, &painting);
        return 0;
    }
    if (message == WM_SIZE) {
        layout_controls(LOWORD(lparam), HIWORD(lparam));
        return 0;
    }
    if (message == WM_VSCROLL) {
        SCROLLINFO scrolling{.cbSize = sizeof(scrolling), .fMask = SIF_ALL};
        GetScrollInfo(window_, SB_VERT, &scrolling);
        auto position = scroll_position_;
        switch (LOWORD(wparam)) {
        case SB_LINEUP: position -= MulDiv(24, dpi_, 96); break;
        case SB_LINEDOWN: position += MulDiv(24, dpi_, 96); break;
        case SB_PAGEUP: position -= static_cast<int>(scrolling.nPage); break;
        case SB_PAGEDOWN: position += static_cast<int>(scrolling.nPage); break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: position = scrolling.nTrackPos; break;
        case SB_TOP: position = scrolling.nMin; break;
        case SB_BOTTOM: position = scrolling.nMax; break;
        default: return 0;
        }
        scroll_to(position);
        return 0;
    }
    if (message == WM_MOUSEWHEEL) {
        const auto lines = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
        scroll_to(scroll_position_ - lines * MulDiv(48, dpi_, 96));
        return 0;
    }
    if (message == WM_DPICHANGED) {
        dpi_ = HIWORD(wparam);
        update_fonts();
        const auto icon_size = MulDiv(60, dpi_, 96);
        const auto icon = LoadImageW(instance_, MAKEINTRESOURCEW(IDI_SIMPILOT), IMAGE_ICON,
                                     icon_size, icon_size, LR_SHARED);
        SendMessageW(icon_, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED) {
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    }
    if (message == WM_NOTIFY) {
        const auto* notification = reinterpret_cast<const NMHDR*>(lparam);
        if (notification && (notification->code == NM_CLICK
                             || notification->code == NM_RETURN)) {
            open_target(static_cast<int>(notification->idFrom));
            return 0;
        }
    }
    if (message == WM_COMMAND) {
        const auto identifier = LOWORD(wparam);
        if (identifier == IDOK || identifier == IDCANCEL) {
            DestroyWindow(window);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        const auto result = DefWindowProcW(window, message, wparam, lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        window_ = nullptr;
        return result;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace simpilot
