#include "settings_window.hpp"
#include "settings_visual_style.hpp"
#include "resource.h"
#include "configuration_backup.hpp"
#include <commctrl.h>
#include <algorithm>

namespace simpilot {
bool SettingsWindow::show_modal(HINSTANCE instance, HWND owner, std::string language,
    const std::filesystem::path& configuration, const SettingsRegistry& pages,
    const SettingsParticipantRegistry& participants, SettingsSession::Commit commit,
    LanguageChangeSink language_change, DiagnosticSink diagnose, BackupSink backup) {
    SettingsWindow window(instance, owner, std::move(language), configuration, pages, participants,
        std::move(commit), std::move(language_change), std::move(diagnose), std::move(backup));
    return window.run();
}
SettingsWindow::SettingsWindow(HINSTANCE instance, HWND owner, std::string language,
    const std::filesystem::path& configuration, const SettingsRegistry& pages,
    const SettingsParticipantRegistry& participants, SettingsSession::Commit commit,
    LanguageChangeSink language_change, DiagnosticSink diagnose, BackupSink backup)
    : instance_(instance), owner_(owner), localization_(std::move(language)), registry_(pages),
      session_(participants, SettingsDocument::load(configuration), [diagnose](std::string_view message) {
          if (diagnose) diagnose(std::wstring(message.begin(), message.end()));
      }), commit_(std::move(commit)), language_change_(std::move(language_change)),
      diagnose_(std::move(diagnose)), backup_(std::move(backup)),
      configuration_directory_(configuration.parent_path()) {}
SettingsWindow::~SettingsWindow() {
    if (window_) DestroyWindow(window_);
    pages_.clear();
    session_.cancel();
    if (font_) DeleteObject(font_);
}
bool SettingsWindow::run() {
    INITCOMMONCONTROLSEX controls{sizeof(controls),
        ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW wc{};
    wc.lpfnWndProc = procedure;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"Simpilot.SettingsWindow";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    dpi_ = GetDpiForSystem();
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT preferred{0, 0, MulDiv(1028, dpi_, 96), MulDiv(684, dpi_, 96)};
    AdjustWindowRectExForDpi(&preferred, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT, dpi_);
    const int width = std::min(static_cast<int>(preferred.right - preferred.left),
        static_cast<int>((monitor.rcWork.right - monitor.rcWork.left) * 92 / 100));
    const int height = std::min(static_cast<int>(preferred.bottom - preferred.top),
        static_cast<int>((monitor.rcWork.bottom - monitor.rcWork.top) * 92 / 100));
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName,
        localization_.text("settings.title").data(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
        monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2,
        width, height, owner_, nullptr, instance_, this);
    if (!window_) return false;
    settings_visual_style::apply_application_icons(window_, instance_, IDI_SIMPILOT);
    const bool disable_owner = owner_ && IsWindowEnabled(owner_);
    if (disable_owner) EnableWindow(owner_, FALSE);
    ShowWindow(window_, SW_SHOW);
    MSG message{};
    int quit = 0;
    bool repost = false;
    while (window_ && IsWindow(window_)) {
        const auto received = GetMessageW(&message, nullptr, 0, 0);
        if (received <= 0) { repost = received == 0; quit = static_cast<int>(message.wParam); break; }
        if (message.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (message.wParam == L'S') { (void)apply(); continue; }
            if (message.wParam == VK_TAB && !pages_.empty()) {
                const auto count = static_cast<int>(pages_.size());
                selected_ = (selected_ + ((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1) + count) % count;
                SendMessageW(navigation_, LB_SETCURSEL, selected_, 0);
                select_page();
                SetFocus(navigation_);
                continue;
            }
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            wchar_t type[16]{};
            GetClassNameW(message.hwnd, type, 16);
            if (_wcsicmp(type, L"Edit") == 0) continue;
        }
        if (!IsDialogMessageW(window_, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (window_) DestroyWindow(window_);
    pages_.clear();
    session_.cancel();
    if (disable_owner) {
        EnableWindow(owner_, TRUE);
        SetForegroundWindow(owner_);
    }
    if (repost) PostQuitMessage(quit);
    return applied_;
}
void SettingsWindow::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    brand_icon_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ICON,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    brand_ = CreateWindowW(L"STATIC", L"Simpilot", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    navigation_label_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    navigation_ = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP
        | LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(250), instance_, nullptr);
    const auto button = [&](int id) {
        return CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP
            | (id == 1 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), 0, 0, 0, 0,
            window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    };
    save_ = button(1); cancel_ = button(2); apply_ = button(3);
    settings_visual_style::style_button(save_, settings_visual_style::ButtonStyle::primary);
    settings_visual_style::style_button(apply_);
    settings_visual_style::style_button(cancel_, settings_visual_style::ButtonStyle::quiet);
    status_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    if (!navigation_ || !save_ || !cancel_ || !apply_ || !status_ || !brand_ || !brand_icon_ || !navigation_label_)
        throw std::runtime_error("Cannot create settings host controls");
    update_font();
    registry_.visit([this](const auto&, const SettingsPageContribution& contribution) {
        auto page = contribution.create();
        if (!page) throw std::runtime_error("Settings page factory returned null");
        page->create({instance_, window_, dpi_, font_, localization_, [this] { changed(); },
            [this](std::string language) { return change_language(std::move(language)); },
            configuration_directory_, [this](BackupAction action) { backup(action); }});
        pages_.push_back({contribution.title_key, std::move(page)});
    });
    // Native dialog navigation follows sibling order, including contributed page children.
    for (const auto control : {cancel_, apply_, save_})
        SetWindowPos(control, HWND_BOTTOM, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    refresh_language(); select_page(); changed();
}
void SettingsWindow::update_font() {
    const auto old = font_;
    font_ = CreateFontW(-MulDiv(14, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, localization_.language_code().starts_with("zh")
            ? L"Microsoft YaHei UI" : L"Segoe UI");
    for (auto control : {navigation_, save_, apply_, cancel_, status_})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    typography_.update(font_, dpi_);
    SendMessageW(brand_, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.section()), TRUE);
    SendMessageW(navigation_label_, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.caption()), TRUE);
    const auto icon = LoadImageW(instance_, MAKEINTRESOURCEW(IDI_SIMPILOT), IMAGE_ICON,
        MulDiv(32, dpi_, 96), MulDiv(32, dpi_, 96), LR_SHARED);
    SendMessageW(brand_icon_, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
    layout();
    if (old) DeleteObject(old);
}
void SettingsWindow::backup(BackupAction action) {
    if (!backup_) return;
    if (session_.dirty()) {
        const bool exporting = action == BackupAction::export_settings;
        const TASKDIALOG_BUTTON buttons[]{
            {100, localization_.text(exporting ? "backup.apply_export" : "backup.apply_import").data()},
            {101, localization_.text(exporting ? "backup.saved_export" : "backup.discard_import").data()}};
        TASKDIALOGCONFIG dialog{sizeof(dialog)};
        dialog.hwndParent = window_;
        dialog.pszWindowTitle = localization_.text("ui.app_title").data();
        dialog.pszMainInstruction = localization_.text("settings.pending_changes").data();
        dialog.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
        dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        dialog.cButtons = static_cast<UINT>(std::size(buttons)); dialog.pButtons = buttons;
        int selected = IDCANCEL;
        if (FAILED(TaskDialogIndirect(&dialog, &selected, nullptr, nullptr)) || selected == IDCANCEL) return;
        if (selected == 100 && !apply()) return;
    }
    if (backup_(window_, action)) {
        session_.cancel();
        DestroyWindow(window_);
        PostQuitMessage(restore_restart_exit_code);
    }
}
void SettingsWindow::layout() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const auto scale = [this](int value) { return MulDiv(value, dpi_, 96); };
    const bool redraw = IsWindowVisible(window_) != FALSE;
    if (redraw) SendMessageW(window_, WM_SETREDRAW, FALSE, 0);
    navigation_width_ = std::min(scale(216), static_cast<int>(client.right * 28 / 100));
    const bool compact_footer = client.right < scale(850);
    footer_height_ = scale(compact_footer ? 96 : 72);
    const int margin = scale(28);
    MoveWindow(brand_icon_, scale(20), scale(24), scale(32), scale(32), TRUE);
    MoveWindow(brand_, scale(62), scale(22), std::max(1, navigation_width_ - scale(74)), scale(22), TRUE);
    MoveWindow(navigation_label_, scale(62), scale(46), std::max(1, navigation_width_ - scale(74)), scale(20), TRUE);
    MoveWindow(navigation_, scale(12), scale(88), std::max(1, navigation_width_ - scale(24)),
        std::max(1L, client.bottom - footer_height_ - scale(100)), TRUE);
    const auto dc = GetDC(navigation_);
    const auto old_font = SelectObject(dc, font_);
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        RECT text{0, 0, std::max(1, navigation_width_ - scale(52)), 0};
        const auto label = localization_.text(pages_[i].title);
        DrawTextW(dc, label.data(), static_cast<int>(label.size()), &text,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SendMessageW(navigation_, LB_SETITEMHEIGHT, i, std::max(scale(48), static_cast<int>(text.bottom) + scale(8)));
    }
    SelectObject(dc, old_font);
    ReleaseDC(navigation_, dc);
    const int available = std::max(1L, client.right - navigation_width_ - margin * 2);
    const int width = std::min(available, scale(840));
    const int left = navigation_width_ + margin;
    RECT content{left, margin, left + width,
        std::max(margin + 1L, client.bottom - footer_height_ - margin)};
    for (const auto& page : pages_) page.instance->layout(content, dpi_, font_);
    const int button_width = std::min(scale(88), static_cast<int>(std::max(1L, client.right / 5)));
    const HWND buttons[]{save_, apply_, cancel_};
    for (int i = 0; i < 3; ++i) MoveWindow(buttons[i],
        client.right - margin - button_width * (i + 1) - scale(8) * i,
        client.bottom - scale(54), button_width, scale(36), TRUE);
    const int buttons_left = client.right - margin - 3 * button_width - scale(16);
    MoveWindow(status_, content.left, client.bottom - scale(compact_footer ? 90 : 56),
        compact_footer ? width : std::max(1L, buttons_left - scale(24) - content.left), scale(40), TRUE);
    if (redraw) {
        SendMessageW(window_, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}
void SettingsWindow::refresh_language() {
    SetWindowTextW(window_, localization_.text("settings.title").data());
    SetWindowTextW(save_, localization_.text("settings.save").data());
    SetWindowTextW(apply_, localization_.text("settings.apply").data());
    SetWindowTextW(cancel_, localization_.text("settings.cancel").data());
    SetWindowTextW(navigation_label_, localization_.text("settings.navigation").data());
    SendMessageW(navigation_, LB_RESETCONTENT, 0, 0);
    for (const auto& page : pages_) {
        SendMessageW(navigation_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(localization_.text(page.title).data()));
        page.instance->refresh_language(localization_);
    }
    SendMessageW(navigation_, LB_SETCURSEL, selected_, 0);
    update_font();
    changed();
}
bool SettingsWindow::change_language(std::string language) {
    if (language.empty()) return false;
    if (language == localization_.language_code()) return true;
    if (language_change_ && !language_change_(language)) return false;
    localization_.set_language(std::move(language));
    refresh_language();
    return true;
}
void SettingsWindow::select_page() {
    for (std::size_t i = 0; i < pages_.size(); ++i)
        pages_[i].instance->show(static_cast<int>(i) == selected_);
}
void SettingsWindow::changed() {
    save_failed_ = false;
    const bool dirty = session_.dirty();
    SetWindowTextW(status_, dirty ? localization_.text("settings.pending_changes").data() : L"");
    EnableWindow(apply_, dirty);
}
void SettingsWindow::paint_background(HDC dc) {
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(dc, &client, settings_visual_style::background_brush());
    RECT sidebar{0, 0, navigation_width_, client.bottom};
    FillRect(dc, &sidebar, settings_visual_style::high_contrast_enabled()
        ? GetSysColorBrush(COLOR_WINDOW) : settings_visual_style::brushes().navigation);
    const RECT footer{navigation_width_, client.bottom - footer_height_, client.right, client.bottom};
    FillRect(dc, &footer, settings_visual_style::background_brush());
    settings_visual_style::draw_separator(dc, navigation_width_, client.right, footer.top, dpi_);
}
bool SettingsWindow::apply() {
    for (const auto& page : pages_) page.instance->show(false);
    select_page();
    const auto result = session_.apply([this](const SettingsDocument& candidate) {
        auto document = candidate;
        const auto language = localization_.language_code();
        document.set(L"General", L"Language", std::wstring(language.begin(), language.end()));
        return commit_ && commit_(document);
    });
    applied_ = applied_ || result;
    changed();
    save_failed_ = !result;
    SetWindowTextW(status_, localization_.text(result ? "settings.applied" : "ui.settings_save_failed").data());
    return result;
}
bool SettingsWindow::request_close() {
    return !session_.dirty() || MessageBoxW(window_, localization_.text("settings.unsaved_changes").data(),
        localization_.text("settings.title").data(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}
LRESULT CALLBACK SettingsWindow::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* instance = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        instance->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
    }
    auto* instance = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!instance) return DefWindowProcW(window, message, wparam, lparam);
    try {
        const auto result = instance->message(message, wparam, lparam);
        if (message == WM_NCDESTROY) instance->window_ = nullptr;
        return result;
    } catch (...) {
        try { if (instance->diagnose_) instance->diagnose_(L"settings host callback failed"); } catch (...) {}
        return message == WM_CREATE ? -1 : 0;
    }
}
LRESULT SettingsWindow::message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
        RECT minimum{0, 0, MulDiv(960, dpi_, 96), MulDiv(684, dpi_, 96)};
        AdjustWindowRectExForDpi(&minimum, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT, dpi_);
        limits->ptMinTrackSize.x = std::min(static_cast<int>(minimum.right - minimum.left),
            static_cast<int>((monitor.rcWork.right - monitor.rcWork.left) * 92 / 100));
        limits->ptMinTrackSize.y = std::min(static_cast<int>(minimum.bottom - minimum.top),
            static_cast<int>((monitor.rcWork.bottom - monitor.rcWork.top) * 92 / 100));
        return 0;
    }
    if (message == WM_CREATE) { create_controls(); return 0; }
    if (message == WM_MEASUREITEM) {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (measure && measure->CtlID == 250) {
            measure->itemHeight = MulDiv(48, dpi_, 96);
            return TRUE;
        }
    }
    if (message == WM_SIZE) { layout(); return 0; }
    if (message == WM_DPICHANGED) {
        dpi_ = HIWORD(wparam);
        const auto* bounds = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window_, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
            bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
        update_font();
        return 0;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == 250 && HIWORD(wparam) == LBN_SELCHANGE) {
            selected_ = std::max(0, static_cast<int>(SendMessageW(navigation_, LB_GETCURSEL, 0, 0)));
            select_page();
        } else if (HIWORD(wparam) == BN_CLICKED) {
            if ((LOWORD(wparam) == 1 || LOWORD(wparam) == 3) && apply() && LOWORD(wparam) == 1)
                DestroyWindow(window_);
            if (LOWORD(wparam) == 2 && request_close()) DestroyWindow(window_);
        }
        return 0;
    }
    if (message == WM_CLOSE) { if (request_close()) DestroyWindow(window_); return 0; }
    if (message == WM_DRAWITEM) {
        const auto* drawing = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (drawing && drawing->CtlID == 250) {
            settings_visual_style::draw_navigation_item(*drawing, navigation_); return TRUE;
        }
    }
    if (message == WM_ERASEBKGND) { paint_background(reinterpret_cast<HDC>(wparam)); return 1; }
    if (message == WM_PAINT) {
        PAINTSTRUCT painting{};
        const auto dc = BeginPaint(window_, &painting);
        paint_background(dc);
        EndPaint(window_, &painting);
        return 0;
    }
    if (message == WM_CTLCOLORSTATIC
        && (reinterpret_cast<HWND>(lparam) == brand_ || reinterpret_cast<HWND>(lparam) == navigation_label_
            || reinterpret_cast<HWND>(lparam) == brand_icon_))
        return settings_visual_style::handle_navigation_color(wparam);
    if (message == WM_CTLCOLORSTATIC && reinterpret_cast<HWND>(lparam) == status_) {
        const auto brush = settings_visual_style::handle_secondary_text(wparam, lparam);
        if (save_failed_ && !settings_visual_style::high_contrast_enabled())
            SetTextColor(reinterpret_cast<HDC>(wparam), RGB(180, 35, 24));
        return brush;
    }
    if (message == WM_CTLCOLORLISTBOX) return settings_visual_style::handle_navigation_color(wparam);
    if (settings_visual_style::is_color_message(message))
        return settings_visual_style::handle_color_message(message, wparam, lparam);
    return DefWindowProcW(window_, message, wparam, lparam);
}
} // namespace simpilot
