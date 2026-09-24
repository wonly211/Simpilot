#include "settings_window.hpp"
#include "settings_visual_style.hpp"
#include "resource.h"
#include <commctrl.h>
#include <algorithm>

namespace simpilot {
bool SettingsWindow::show_modal(HINSTANCE instance, HWND owner, std::string language,
    const std::filesystem::path& configuration, const SettingsRegistry& pages,
    const SettingsParticipantRegistry& participants, SettingsSession::Commit commit,
    LanguageChangeSink language_change, DiagnosticSink diagnose) {
    SettingsWindow window(instance, owner, std::move(language), configuration, pages, participants,
        std::move(commit), std::move(language_change), std::move(diagnose));
    return window.run();
}
SettingsWindow::SettingsWindow(HINSTANCE instance, HWND owner, std::string language,
    const std::filesystem::path& configuration, const SettingsRegistry& pages,
    const SettingsParticipantRegistry& participants, SettingsSession::Commit commit,
    LanguageChangeSink language_change, DiagnosticSink diagnose)
    : instance_(instance), owner_(owner), localization_(std::move(language)), registry_(pages),
      session_(participants, SettingsDocument::load(configuration), [diagnose](std::string_view message) {
          if (diagnose) diagnose(std::wstring(message.begin(), message.end()));
      }), commit_(std::move(commit)), language_change_(std::move(language_change)),
      diagnose_(std::move(diagnose)) {}
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
    const int width = std::min(MulDiv(1080, dpi_, 96),
        static_cast<int>((monitor.rcWork.right - monitor.rcWork.left) * 92 / 100));
    const int height = std::min(MulDiv(760, dpi_, 96),
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
    if (disable_owner) EnableWindow(owner_, TRUE);
    if (repost) PostQuitMessage(quit);
    return applied_;
}
void SettingsWindow::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    navigation_ = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP
        | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(250), instance_, nullptr);
    const auto button = [&](int id) {
        return CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP
            | (id == 1 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), 0, 0, 0, 0,
            window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    };
    save_ = button(1); cancel_ = button(2); apply_ = button(3);
    status_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    if (!navigation_ || !save_ || !cancel_ || !apply_ || !status_)
        throw std::runtime_error("Cannot create settings host controls");
    update_font();
    registry_.visit([this](const auto&, const SettingsPageContribution& contribution) {
        auto page = contribution.create();
        if (!page) throw std::runtime_error("Settings page factory returned null");
        page->create({instance_, window_, dpi_, font_, localization_, [this] { changed(); },
            [this](std::string language) { return change_language(std::move(language)); }});
        pages_.push_back({contribution.title_key, std::move(page)});
    });
    refresh_language(); select_page(); changed();
}
void SettingsWindow::update_font() {
    const auto old = font_;
    font_ = CreateFontW(-MulDiv(13, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    for (auto control : {navigation_, save_, apply_, cancel_, status_})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    layout();
    if (old) DeleteObject(old);
}
void SettingsWindow::layout() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const auto scale = [this](int value) { return MulDiv(value, dpi_, 96); };
    const int navigation_width = std::min(scale(230), static_cast<int>(client.right * 30 / 100));
    const int footer = scale(64), margin = scale(24);
    SendMessageW(navigation_, LB_SETITEMHEIGHT, 0, scale(42));
    MoveWindow(navigation_, 0, 0, navigation_width, std::max(1L, client.bottom - footer), TRUE);
    RECT content{navigation_width + margin, margin,
        std::max(navigation_width + margin + 1L, client.right - margin),
        std::max(margin + 1L, client.bottom - footer - margin)};
    for (const auto& page : pages_) page.instance->layout(content, dpi_, font_);
    const int button_width = std::min(scale(96), static_cast<int>(std::max(1L, client.right / 5)));
    const HWND buttons[]{cancel_, apply_, save_};
    for (int i = 0; i < 3; ++i) MoveWindow(buttons[i],
        client.right - margin - button_width * (i + 1) - scale(10) * i,
        client.bottom - scale(50), button_width, scale(36), TRUE);
    MoveWindow(status_, content.left, client.bottom - scale(48),
        std::max(1L, content.right - content.left - (button_width + scale(10)) * 3), scale(32), TRUE);
}
void SettingsWindow::refresh_language() {
    SetWindowTextW(window_, localization_.text("settings.title").data());
    SetWindowTextW(save_, localization_.text("settings.save").data());
    SetWindowTextW(apply_, localization_.text("settings.apply").data());
    SetWindowTextW(cancel_, localization_.text("settings.cancel").data());
    SendMessageW(navigation_, LB_RESETCONTENT, 0, 0);
    for (const auto& page : pages_) {
        SendMessageW(navigation_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(localization_.text(page.title).data()));
        page.instance->refresh_language(localization_);
    }
    SendMessageW(navigation_, LB_SETCURSEL, selected_, 0);
    layout();
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
void SettingsWindow::changed() { SetWindowTextW(status_, L""); EnableWindow(apply_, session_.dirty()); }
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
        limits->ptMinTrackSize.x = std::min(MulDiv(960, dpi_, 96),
            static_cast<int>((monitor.rcWork.right - monitor.rcWork.left) * 92 / 100));
        limits->ptMinTrackSize.y = std::min(MulDiv(680, dpi_, 96),
            static_cast<int>((monitor.rcWork.bottom - monitor.rcWork.top) * 92 / 100));
        return 0;
    }
    if (message == WM_CREATE) { create_controls(); return 0; }
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
    if (message == WM_ERASEBKGND) return settings_visual_style::erase_background(window_, wparam);
    if (message == WM_CTLCOLORLISTBOX) return settings_visual_style::handle_navigation_color(wparam);
    if (settings_visual_style::is_color_message(message))
        return settings_visual_style::handle_color_message(message, wparam, lparam);
    return DefWindowProcW(window_, message, wparam, lparam);
}
} // namespace simpilot
