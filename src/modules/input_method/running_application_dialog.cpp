#include "running_applications.hpp"

#include "input_method_model.hpp"
#include "settings_visual_style.hpp"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <utility>

namespace simpilot::input_method {
namespace {

constexpr int search_id = 201, refresh_id = 202, background_id = 203, list_id = 204;
constexpr int path_id = 205, status_id = 206;
constexpr UINT_PTR dispatch_watchdog = 1;
constexpr DWORD window_style = WS_CAPTION | WS_SYSMENU | WS_POPUP
    | WS_THICKFRAME | WS_CLIPCHILDREN;
constexpr DWORD window_exstyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

HMENU identifier(int value) { return reinterpret_cast<HMENU>(static_cast<INT_PTR>(value)); }
void text(HWND window, std::wstring_view value) {
    SetWindowTextW(window, std::wstring(value).c_str());
}
std::wstring read_text(HWND window) {
    std::wstring result(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    const auto length = GetWindowTextW(window, result.data(), static_cast<int>(result.size()));
    result.resize(static_cast<std::size_t>(std::max(0, length)));
    return result;
}

class ApplicationDialog final {
public:
    ApplicationDialog(HINSTANCE instance, HWND owner, const Localization& localization,
        const ApplicationSelectionServices& services)
        : instance_(instance), owner_(owner), localization_(localization),
          enumerate_(services.enumerate ? services.enumerate : enumerate_running_applications),
          dispatcher_(services.dispatcher) {
        if (!dispatcher_) {
            local_dispatcher_ = std::make_unique<UiDispatcher>(instance);
            dispatcher_ = local_dispatcher_.get();
        }
    }
    ~ApplicationDialog() {
        cancel_work();
        if (window_) DestroyWindow(window_);
        if (icon_font_) DeleteObject(icon_font_);
    }

    std::optional<std::wstring> run() {
        const WNDCLASSW klass{
            .lpfnWndProc = procedure, .hInstance = instance_,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .lpszClassName = L"Simpilot.InputMethodApplicationDialog"};
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return std::nullopt;
        dpi_ = owner_ ? GetDpiForWindow(owner_) : GetDpiForSystem();
        window_ = CreateWindowExW(window_exstyle, klass.lpszClassName,
            localized("settings.input_method.process_title").data(), window_style,
            CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, owner_, nullptr, instance_, this);
        if (!window_) return std::nullopt;
        create_controls();
        position();
        const bool owner_enabled = owner_ && IsWindowEnabled(owner_);
        if (owner_enabled) EnableWindow(owner_, FALSE);
        ShowWindow(window_, SW_SHOW);
        SetFocus(search_);
        refresh();
        MSG message{};
        bool quit = false;
        int quit_code = 0;
        while (window_) {
            const auto received = GetMessageW(&message, nullptr, 0, 0);
            if (received <= 0) {
                quit = received == 0;
                quit_code = static_cast<int>(message.wParam);
                break;
            }
            if (!IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (window_) DestroyWindow(window_);
        if (owner_enabled && IsWindow(owner_)) EnableWindow(owner_, TRUE);
        if (IsWindow(owner_)) SetActiveWindow(owner_);
        if (quit) PostQuitMessage(quit_code);
        return result_;
    }

private:
    int scale(int value) const { return MulDiv(value, dpi_, 96); }
    std::wstring_view localized(const char* key) const { return localization_.text(key); }
    HWND control(const wchar_t* klass, int id, DWORD style = 0, DWORD exstyle = 0) {
        const auto window = CreateWindowExW(exstyle, klass, L"",
            WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window_, identifier(id), instance_, nullptr);
        if (!window) throw std::runtime_error("Cannot create application picker control");
        return window;
    }

    void create_controls() {
        heading_ = control(L"STATIC", 207);
        search_ = control(L"EDIT", search_id, WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
        refresh_ = control(L"BUTTON", refresh_id, WS_TABSTOP);
        background_ = control(L"BUTTON", background_id, WS_TABSTOP | BS_AUTOCHECKBOX);
        list_ = control(WC_LISTVIEWW, list_id,
            WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
        path_label_ = control(L"STATIC", 208);
        path_ = control(L"EDIT", path_id, WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL);
        status_ = control(L"STATIC", status_id);
        cancel_ = control(L"BUTTON", IDCANCEL, WS_TABSTOP);
        select_ = control(L"BUTTON", IDOK, WS_TABSTOP | BS_DEFPUSHBUTTON);
        text(heading_, localized("settings.input_method.process_title"));
        text(search_, L"");
        SendMessageW(search_, EM_SETCUEBANNER, TRUE,
            reinterpret_cast<LPARAM>(localized("settings.input_method.process_search").data()));
        text(refresh_, L"\xE72C");
        text(background_, localized("settings.input_method.process_background"));
        text(path_label_, localized("settings.input_method.path"));
        text(select_, localized("settings.input_method.process_select"));
        text(cancel_, localized("settings.cancel"));
        ListView_SetExtendedListViewStyle(list_,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_INFOTIP);
        settings_visual_style::style_list_view(list_);
        settings_visual_style::style_button(refresh_);
        settings_visual_style::set_button_tooltip(refresh_, localized("settings.input_method.process_refresh"));
        settings_visual_style::style_button(select_, settings_visual_style::ButtonStyle::primary);
        settings_visual_style::style_button(cancel_, settings_visual_style::ButtonStyle::quiet);
        update_fonts();
        selection_changed();
    }

    void update_fonts() {
        typography_.update(nullptr, dpi_);
        for (auto child : {heading_, search_, refresh_, background_, list_, path_label_,
                path_, status_, cancel_, select_})
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.body()), TRUE);
        SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.title()), TRUE);
        for (auto child : {path_label_, status_})
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.caption()), TRUE);
        const auto previous = icon_font_;
        icon_font_ = CreateFontW(-scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, L"Segoe MDL2 Assets");
        SendMessageW(refresh_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_), TRUE);
        if (previous) DeleteObject(previous);
    }

    RECT work_area(const RECT* proposed = nullptr) const {
        MONITORINFO monitor{.cbSize = sizeof(monitor)};
        const auto display = proposed ? MonitorFromRect(proposed, MONITOR_DEFAULTTONEAREST)
            : MonitorFromWindow(window_ ? window_ : owner_, MONITOR_DEFAULTTONEAREST);
        GetMonitorInfoW(display, &monitor);
        return monitor.rcWork;
    }

    void position() {
        RECT outer{0, 0, scale(780), scale(500)};
        AdjustWindowRectExForDpi(&outer, window_style, FALSE, window_exstyle, dpi_);
        MONITORINFO monitor{.cbSize = sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &monitor);
        const auto work = monitor.rcWork;
        const auto width = std::min(outer.right - outer.left, work.right - work.left);
        const auto height = std::min(outer.bottom - outer.top, work.bottom - work.top);
        SetWindowPos(window_, nullptr, work.left + (work.right - work.left - width) / 2,
            work.top + (work.bottom - work.top - height) / 2, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
    }

    void layout() {
        if (!list_) return;
        RECT bounds{};
        GetClientRect(window_, &bounds);
        const int margin = scale(24), gap = scale(12), width = bounds.right - margin * 2;
        MoveWindow(heading_, margin, scale(20), width, scale(32), TRUE);
        const int search_width = std::max(scale(100), width - scale(256));
        MoveWindow(search_, margin, scale(72), search_width, scale(32), TRUE);
        MoveWindow(refresh_, margin + search_width + gap, scale(72), scale(40), scale(32), TRUE);
        MoveWindow(background_, margin + search_width + scale(64), scale(72),
            scale(192), scale(32), TRUE);
        const int list_bottom = std::max(scale(160), static_cast<int>(bounds.bottom) - scale(144));
        MoveWindow(list_, margin, scale(120), width, list_bottom - scale(120), TRUE);
        MoveWindow(path_label_, margin, list_bottom + scale(8), width, scale(16), TRUE);
        MoveWindow(path_, margin, list_bottom + scale(32), width, scale(28), TRUE);
        MoveWindow(status_, margin, list_bottom + scale(64), width, scale(20), TRUE);
        MoveWindow(cancel_, bounds.right - margin - scale(188), bounds.bottom - scale(44),
            scale(88), scale(32), TRUE);
        MoveWindow(select_, bounds.right - margin - scale(88), bounds.bottom - scale(44),
            scale(88), scale(32), TRUE);
        const int available = std::max(scale(300), width - GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_));
        const std::array<int, 3> widths{scale(156), (available - scale(156)) * 2 / 5,
            (available - scale(156)) * 3 / 5};
        constexpr std::array<const char*, 3> keys{"settings.input_method.application",
            "settings.input_method.process_window", "settings.input_method.process_path"};
        const auto count = Header_GetItemCount(ListView_GetHeader(list_));
        for (int index = 0; index < 3; ++index) {
            std::wstring label(localized(keys[index]));
            LVCOLUMNW column{.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM,
                .cx = widths[index], .pszText = label.data(), .iSubItem = index};
            if (index < count) ListView_SetColumn(list_, index, &column);
            else ListView_InsertColumn(list_, index, &column);
        }
        InvalidateRect(window_, nullptr, TRUE);
    }

    const RunningApplication* selected_application() const {
        const auto selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (selected < 0 || static_cast<std::size_t>(selected) >= rows_.size()) return nullptr;
        return &snapshot_.applications[rows_[static_cast<std::size_t>(selected)]];
    }

    void selection_changed() {
        const auto* selected = selected_application();
        EnableWindow(select_, selected && !busy_);
        text(path_, selected ? selected->executable_path : L"");
    }

    void refresh_rows(std::wstring identity = {}) {
        if (identity.empty()) {
            if (const auto* selected = selected_application())
                identity = normalize_path(selected->executable_path);
        }
        rows_.clear();
        ListView_DeleteAllItems(list_);
        rows_ = filter_running_applications(snapshot_,
            SendMessageW(background_, BM_GETCHECK, 0, 0) == BST_CHECKED, read_text(search_));
        int selected_row = -1;
        for (std::size_t row = 0; row < rows_.size(); ++row) {
            const auto& application = snapshot_.applications[rows_[row]];
            auto name = std::filesystem::path(application.executable_path).filename().wstring();
            LVITEMW item{.mask = LVIF_TEXT, .iItem = static_cast<int>(row), .pszText = name.data()};
            ListView_InsertItem(list_, &item);
            auto title = application_window_titles(application);
            ListView_SetItemText(list_, static_cast<int>(row), 1, title.data());
            ListView_SetItemText(list_, static_cast<int>(row), 2,
                const_cast<wchar_t*>(application.executable_path.c_str()));
            if (normalize_path(application.executable_path) == identity)
                selected_row = static_cast<int>(row);
        }
        if (selected_row >= 0) {
            ListView_SetItemState(list_, selected_row, LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list_, selected_row, FALSE);
        }
        const auto key = busy_ ? "settings.input_method.process_loading"
            : snapshot_.error ? "settings.input_method.process_failed"
            : snapshot_.applications.empty() ? "settings.input_method.process_empty"
            : !read_text(search_).empty() ? "settings.input_method.process_no_match"
            : "settings.input_method.process_no_windows";
        settings_visual_style::set_list_empty_text(list_, localized(key));
        if (snapshot_.error) {
            text(status_, std::wstring(localized("settings.input_method.process_failed"))
                + L" (" + std::to_wstring(snapshot_.error) + L")");
        } else text(status_, busy_ ? localized("settings.input_method.process_loading") : L"");
        selection_changed();
    }

    void refresh() {
        if (busy_) return;
        if (worker_.joinable()) worker_.join();
        std::wstring identity;
        if (const auto* selected = selected_application())
            identity = normalize_path(selected->executable_path);
        busy_ = true;
        dispatch_failed_ = false;
        EnableWindow(refresh_, FALSE);
        refresh_rows(identity);
        SetTimer(window_, dispatch_watchdog, 100, nullptr);
        try {
            worker_ = std::jthread([this, identity = std::move(identity)](std::stop_token stop) {
                ApplicationSnapshot snapshot;
                try { snapshot = merge_running_applications(enumerate_(stop)); }
                catch (...) { snapshot.error = ERROR_GEN_FAILURE; }
                if (stop.stop_requested()) return;
                try {
                    const bool posted = dispatcher_->post(scope_,
                        [this, identity, snapshot = std::move(snapshot)]() mutable {
                            KillTimer(window_, dispatch_watchdog);
                            if (const auto* selected = selected_application())
                                identity = normalize_path(selected->executable_path);
                            // Old row indices must not refer into the replacement snapshot.
                            rows_.clear();
                            snapshot_ = std::move(snapshot);
                            busy_ = false;
                            EnableWindow(refresh_, TRUE);
                            refresh_rows(std::move(identity));
                        });
                    if (!posted) dispatch_failed_ = true;
                } catch (...) { dispatch_failed_ = true; }
            });
        } catch (...) {
            KillTimer(window_, dispatch_watchdog);
            busy_ = false;
            snapshot_.error = ERROR_NOT_ENOUGH_MEMORY;
            EnableWindow(refresh_, TRUE);
            refresh_rows();
        }
    }

    void cancel_work() noexcept {
        scope_.cancel();
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
    }

    void accept() {
        if (busy_) return;
        if (const auto* selected = selected_application()) {
            result_ = selected->executable_path;
            DestroyWindow(window_);
        }
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            auto* self = static_cast<ApplicationDialog*>(
                reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto* self = reinterpret_cast<ApplicationDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return DefWindowProcW(window, message, wparam, lparam);
        try {
            if (message == WM_COMMAND) {
                const auto id = LOWORD(wparam), notification = HIWORD(wparam);
                if (id == IDCANCEL) DestroyWindow(window);
                else if (id == IDOK) self->accept();
                else if (id == refresh_id && notification == BN_CLICKED) self->refresh();
                else if ((id == search_id && notification == EN_CHANGE)
                    || (id == background_id && notification == BN_CLICKED)) self->refresh_rows();
                return 0;
            }
            if (message == WM_NOTIFY) {
                const auto* header = reinterpret_cast<NMHDR*>(lparam);
                if (header->idFrom == list_id) {
                    if (header->code == LVN_ITEMCHANGED) self->selection_changed();
                    else if (header->code == NM_DBLCLK || header->code == NM_RETURN) self->accept();
                    else if (header->code == LVN_GETINFOTIPW) {
                        auto* tip = reinterpret_cast<NMLVGETINFOTIPW*>(lparam);
                        if (tip->iItem >= 0 && static_cast<std::size_t>(tip->iItem) < self->rows_.size()) {
                            const auto& application = self->snapshot_.applications[
                                self->rows_[static_cast<std::size_t>(tip->iItem)]];
                            const auto value = application_window_titles(application)
                                + L"\r\n" + application.executable_path;
                            wcsncpy_s(tip->pszText, tip->cchTextMax, value.c_str(), _TRUNCATE);
                        }
                    }
                }
            }
            if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
            if (message == WM_TIMER && wparam == dispatch_watchdog) {
                if (self->dispatch_failed_.exchange(false)) {
                    KillTimer(window, dispatch_watchdog);
                    self->busy_ = false;
                    self->snapshot_ = {{}, ERROR_INVALID_STATE};
                    self->rows_.clear();
                    EnableWindow(self->refresh_, TRUE);
                    self->refresh_rows();
                }
                return 0;
            }
            if (message == WM_SIZE) { self->layout(); return 0; }
            if (message == WM_GETMINMAXINFO) {
                auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
                RECT minimum{0, 0, self->scale(640), self->scale(420)};
                AdjustWindowRectExForDpi(&minimum, window_style, FALSE, window_exstyle, self->dpi_);
                const auto work = self->work_area();
                limits->ptMinTrackSize = {std::min(minimum.right - minimum.left, work.right - work.left),
                    std::min(minimum.bottom - minimum.top, work.bottom - work.top)};
                limits->ptMaxTrackSize = {work.right - work.left, work.bottom - work.top};
                return 0;
            }
            if (message == WM_DPICHANGED) {
                self->dpi_ = HIWORD(wparam);
                self->update_fonts();
                auto desired = *reinterpret_cast<RECT*>(lparam);
                const auto work = self->work_area(&desired);
                const auto width = std::min(desired.right - desired.left, work.right - work.left);
                const auto height = std::min(desired.bottom - desired.top, work.bottom - work.top);
                SetWindowPos(window, nullptr,
                    std::clamp(desired.left, work.left, work.right - width),
                    std::clamp(desired.top, work.top, work.bottom - height), width, height,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                self->layout();
                return 0;
            }
            if (message == WM_PAINT) {
                PAINTSTRUCT paint{};
                auto dc = BeginPaint(window, &paint);
                RECT bounds{};
                GetClientRect(window, &bounds);
                settings_visual_style::draw_separator(dc, 0, bounds.right,
                    bounds.bottom - self->scale(60), self->dpi_);
                EndPaint(window, &paint);
                return 0;
            }
            if (message == WM_ERASEBKGND)
                return settings_visual_style::erase_background(window, wparam);
            if (settings_visual_style::is_color_message(message))
                return settings_visual_style::handle_color_message(message, wparam, lparam);
        } catch (...) {
            if (self->status_) text(self->status_, self->localized("settings.input_method.process_failed"));
        }
        if (message == WM_NCDESTROY) {
            KillTimer(window, dispatch_watchdog);
            self->cancel_work();
            self->window_ = nullptr;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    HINSTANCE instance_;
    HWND owner_, window_ = nullptr;
    const Localization& localization_;
    ApplicationEnumerator enumerate_;
    std::unique_ptr<UiDispatcher> local_dispatcher_;
    UiDispatcher* dispatcher_;
    DispatchScope scope_;
    std::jthread worker_;
    ApplicationSnapshot snapshot_;
    std::vector<std::size_t> rows_;
    std::optional<std::wstring> result_;
    bool busy_ = false;
    std::atomic_bool dispatch_failed_ = false;
    UINT dpi_ = 96;
    settings_visual_style::PageTypography typography_;
    HFONT icon_font_ = nullptr;
    HWND heading_{}, search_{}, refresh_{}, background_{}, list_{};
    HWND path_label_{}, path_{}, status_{}, select_{}, cancel_{};
};

std::optional<std::wstring> choose_exe(HWND owner, const Localization& localization) {
    std::wstring buffer(32768, L'\0');
    std::wstring filter(localization.text("settings.input_method.exe_filter"));
    filter.push_back(L'\0');
    filter.append(L"*.exe");
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    OPENFILENAMEW request{
        .lStructSize = sizeof(request), .hwndOwner = owner,
        .lpstrFilter = filter.c_str(), .lpstrFile = buffer.data(),
        .nMaxFile = static_cast<DWORD>(buffer.size()),
        .Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR};
    if (!GetOpenFileNameW(&request)) return std::nullopt;
    buffer.resize(wcslen(buffer.c_str()));
    return buffer;
}

} // namespace

std::optional<std::wstring> show_running_application_dialog(
    HINSTANCE instance, HWND owner, const Localization& localization,
    const ApplicationSelectionServices& services) {
    return ApplicationDialog(instance, owner, localization, services).run();
}

ApplicationSource show_application_source_menu(HWND owner, HWND anchor, const Localization& localization) {
    const auto menu = CreatePopupMenu();
    if (!menu) return ApplicationSource::cancel;
    const auto file = std::wstring(localization.text("settings.input_method.source_file"));
    const auto running = std::wstring(localization.text("settings.input_method.source_running"));
    if (!AppendMenuW(menu, MF_STRING, 1, file.c_str())
        || !AppendMenuW(menu, MF_STRING, 2, running.c_str())) {
        DestroyMenu(menu);
        return ApplicationSource::cancel;
    }
    RECT bounds{};
    GetWindowRect(anchor ? anchor : owner, &bounds);
    const auto selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY
        | TPM_LEFTALIGN | TPM_TOPALIGN, bounds.left, bounds.bottom, owner, nullptr);
    DestroyMenu(menu);
    return selected == 1 ? ApplicationSource::file
        : selected == 2 ? ApplicationSource::running : ApplicationSource::cancel;
}

std::optional<std::wstring> choose_application(
    HINSTANCE instance, HWND owner, HWND anchor, const Localization& localization,
    const ApplicationSelectionServices& services) {
    const auto source = services.choose_source
        ? services.choose_source(owner, anchor, localization)
        : show_application_source_menu(owner, anchor, localization);
    if (source == ApplicationSource::file)
        return services.choose_file ? services.choose_file(owner, localization) : choose_exe(owner, localization);
    if (source == ApplicationSource::running)
        return show_running_application_dialog(instance, owner, localization, services);
    return std::nullopt;
}

} // namespace simpilot::input_method
