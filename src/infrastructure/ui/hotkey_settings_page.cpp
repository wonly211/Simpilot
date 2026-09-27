#include "hotkey_settings_page.hpp"
#include "hotkey_capture_button.hpp"
#include "keyboard_manager.hpp"
#include "settings_visual_style.hpp"
#include "toggle_switch.hpp"

#include <array>
#include <format>

namespace simpilot {

bool confirm_hotkey_availability(HWND owner, const Localization& localization,
    BuiltInHotKey& value, const std::function<bool(const HotKeyGesture&)>& available) {
    if (!value.binding.gesture) return false;
    const auto& gesture = *value.binding.gesture;
    const auto letter = windows_letter_hotkey_index(gesture);
    if (letter && *letter == static_cast<std::size_t>(L'L' - L'A')) {
        MessageBoxW(owner, localization.text("settings.win_l_unsupported").data(),
                    localization.text("settings.title").data(), MB_OK | MB_ICONWARNING);
        return false;
    }
    value.binding.force_override = is_supported_windows_letter_hotkey(gesture);
    if (!value.binding.force_override && available && !available(gesture)) {
        const auto label = gesture.display_text();
        const auto message = std::vformat(localization.text("settings.conflict.registered_security"),
                                         std::make_wformat_args(label));
        if (MessageBoxW(owner, message.c_str(), localization.text("settings.title").data(),
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
        value.binding.force_override = true;
    }
    return true;
}

bool confirm_hotkey_replacement(HWND owner, const Localization& localization,
    const HotKeyGesture& gesture, const HotkeyDraftBinding& conflict) {
    const auto label = gesture.display_text();
    const auto name = localization.text(conflict.label_key);
    const auto message = std::vformat(localization.text("settings.conflict.builtin_duplicate"),
                                     std::make_wformat_args(label, name));
    return MessageBoxW(owner, message.c_str(), localization.text("settings.title").data(),
                      MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

namespace {
class HotkeySettingsPage final : public ISettingsPage {
public:
    HotkeySettingsPage(HotkeyRegistry& hotkeys, KeyboardManager& keyboard,
        std::function<bool(const HotKeyGesture&)> available,
        std::function<void(std::wstring_view)> diagnose)
        : hotkeys_(hotkeys), keyboard_(keyboard), available_(std::move(available)),
          diagnose_(std::move(diagnose)) {}
    ~HotkeySettingsPage() override {
        cancel_capture();
        observer_.reset();
        sections_.clear();
        if (window_) DestroyWindow(window_);
    }
    void create(const SettingsPageContext& context) override {
        instance_ = context.instance;
        changed_ = context.changed;
        localization_ = &context.localization;
        WNDCLASSW wc{};
        wc.lpfnWndProc = procedure;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"Simpilot.HotkeySettingsPage";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Cannot register hotkey settings page");
        }
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 0, 0, context.parent,
            nullptr, instance_, this);
        if (!window_) throw std::runtime_error("Cannot create hotkey settings page");
        heading_ = control(L"STATIC", 0, 0);
        status_ = control(L"STATIC", 0, 0);
        section_heading_ = control(L"STATIC", 0, 0);
        hotkey_heading_ = control(L"STATIC", 0, 0);
        enabled_heading_ = control(L"STATIC", 0, 0);
        for (const auto& draft : hotkeys_.editable_drafts()) {
            if (draft.common_row) add_row(draft);
        }
        hotkeys_.sections.visit([&](const auto&, const SettingsPageContribution& section) {
            auto page = section.create();
            page->create({instance_, window_, context.dpi, context.font, context.localization,
                [this] { hotkeys_.drafts_changed(); }});
            sections_.push_back(std::move(page));
        });
        observer_ = hotkeys_.observe_drafts("settings.hotkey_page", [this] {
            refresh_rows();
            if (changed_) changed_();
        });
        refresh_language(context.localization);
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        dpi_ = dpi;
        font_ = font;
        typography_.update(font, dpi);
        MoveWindow(window_, bounds.left, bounds.top, std::max(1L, bounds.right - bounds.left),
                   std::max(1L, bounds.bottom - bounds.top), TRUE);
        arrange();
    }
    void show(bool visible) override {
        if (!visible) cancel_capture();
        ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
        for (auto& section : sections_) section->show(visible);
        if (visible) refresh_rows();
    }
    void refresh_language(const Localization& localization) override {
        localization_ = &localization;
        SetWindowTextW(heading_, localization.text("settings.global_hotkeys.heading").data());
        SetWindowTextW(section_heading_, localization.text("settings.section.built_in_hotkeys").data());
        SetWindowTextW(hotkey_heading_, localization.text("settings.hotkeys.column.gesture").data());
        SetWindowTextW(enabled_heading_, localization.text("settings.hotkeys.column.enabled").data());
        if (capture_failed_) SetWindowTextW(status_, localization.text("settings.capture_failed").data());
        refresh_rows();
        for (auto& section : sections_) section->refresh_language(localization);
    }
private:
    struct Row {
        HotkeyDraftBinding draft;
        HWND label, capture, clear, enabled;
    };
    HWND control(const wchar_t* type, int id, DWORD style) {
        const auto result = CreateWindowW(type, L"", WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_, nullptr);
        if (!result) throw std::runtime_error("Cannot create hotkey settings control");
        return result;
    }
    void add_row(const HotkeyDraftBinding& draft) {
        if (rows_.size() >= 1000) throw std::runtime_error("Too many hotkey settings rows");
        const auto id = 100 + static_cast<int>(rows_.size()) * 3;
        rows_.push_back({draft, control(L"STATIC", 0, SS_LEFT),
            control(L"BUTTON", id, WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY),
            control(L"BUTTON", id + 1, WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY),
            toggle_switch::create(instance_, window_, id + 2, L"", false)});
        if (!rows_.back().enabled) throw std::runtime_error("Cannot create hotkey enable switch");
        SetWindowLongPtrW(rows_.back().enabled, GWL_STYLE,
            GetWindowLongPtrW(rows_.back().enabled, GWL_STYLE) | BS_NOTIFY);
        settings_visual_style::style_button(rows_.back().capture);
        settings_visual_style::style_button(rows_.back().clear, settings_visual_style::ButtonStyle::clear);
    }
    void refresh_rows() {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            auto& row = rows_[i];
            const auto active = row.draft.active();
            const auto value = active ? row.draft.read() : BuiltInHotKey{};
            SetWindowTextW(row.label, localization_->text(row.draft.label_key).data());
            const auto clear_name = std::wstring(localization_->text("settings.clear")) + L" "
                + std::wstring(localization_->text(row.draft.label_key));
            SetWindowTextW(row.clear, clear_name.c_str());
            const auto title = capturing_ == i || !value.binding.gesture
                ? hotkey_capture_button_text(value.binding.gesture, capturing_ == i, *localization_)
                : value.binding.gesture->display_text();
            SetWindowTextW(row.capture, title.c_str());
            settings_visual_style::style_button(row.capture, capturing_ == i
                ? settings_visual_style::ButtonStyle::recording : settings_visual_style::ButtonStyle::capture);
            const auto tooltip = hotkey_capture_button_text(value.binding.gesture, capturing_ == i, *localization_);
            settings_visual_style::set_button_tooltip(row.capture, tooltip);
            settings_visual_style::set_button_tooltip(row.clear, clear_name);
            const auto label = localization_->text(row.draft.label_key);
            const auto name = std::vformat(localization_->text("settings.enable_hotkey"),
                std::make_wformat_args(label));
            SetWindowTextW(row.enabled, name.c_str());
            SendMessageW(row.enabled, BM_SETCHECK, value.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            EnableWindow(row.capture, active);
            EnableWindow(row.clear, active && value.binding.gesture.has_value());
            EnableWindow(row.enabled, active && value.binding.gesture.has_value());
        }
    }
    int scale(int value) const { return MulDiv(value, dpi_, 96); }
    void arrange() {
        if (!window_) return;
        RECT client{};
        GetClientRect(window_, &client);
        const int height = std::max(1L, client.bottom);
        // Decide wrapping before showing the scrollbar so its appearance cannot flip the layout.
        RECT outer{};
        GetWindowRect(window_, &outer);
        compact_ = outer.right - outer.left < scale(492);
        const int row_height = scale(compact_ ? 88 : 56);
        const int section_height = scale(216);
        const int section_top = scale(80) + static_cast<int>(rows_.size()) * row_height
            + scale(capture_failed_ ? 56 : 16);
        const int content_height = section_top
            + static_cast<int>(sections_.size()) * section_height;
        scroll_ = std::clamp(scroll_, 0, std::max(0, content_height - height));
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS,
            0, content_height - 1, static_cast<UINT>(height), scroll_, 0};
        SetScrollInfo(window_, SB_VERT, &info, TRUE);
        // Updating the scroll range can change the client width.
        GetClientRect(window_, &client);
        const int width = std::max(1L, client.right);
        const int gap = scale(12);
        const int toggle_width = scale(44);
        const int clear_width = scale(32);
        const int trailing_width = scale(96);
        const int label_width = compact_ ? width : scale(132);
        const int capture_x = compact_ ? 0 : scale(144);
        const int capture_width = std::max(1,
            std::min(scale(228), width - capture_x - trailing_width));
        const int clear_x = capture_x + capture_width + scale(8);
        const int toggle_x = clear_x + clear_width + gap;
        auto place = [&](HWND control, int x, int y, int w, int h, HFONT font = nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : typography_.body()), TRUE);
            MoveWindow(control, x, y - scroll_, w, h, TRUE);
        };
        place(heading_, 0, 0, width, scale(36), typography_.title());
        place(section_heading_, 0, scale(56), label_width, scale(20), typography_.section());
        place(hotkey_heading_, capture_x, scale(56), capture_width, scale(20), typography_.caption());
        place(enabled_heading_, toggle_x, scale(56), toggle_width, scale(20), typography_.caption());
        ShowWindow(hotkey_heading_, compact_ ? SW_HIDE : SW_SHOWNA);
        ShowWindow(enabled_heading_, compact_ ? SW_HIDE : SW_SHOWNA);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const int y = scale(80) + static_cast<int>(i) * row_height;
            RECT measured{0, 0, label_width, 0};
            const auto dc = GetDC(window_);
            const auto old = SelectObject(dc, typography_.body());
            const auto label = localization_->text(rows_[i].draft.label_key);
            DrawTextW(dc, label.data(), static_cast<int>(label.size()), &measured,
                DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old);
            ReleaseDC(window_, dc);
            const int label_height = std::min(scale(40), static_cast<int>(measured.bottom));
            place(rows_[i].label, 0, y + (compact_ ? 0 : (row_height - label_height) / 2),
                label_width, compact_ ? scale(40) : label_height);
            const int control_y = y + scale(compact_ ? 42 : 10);
            place(rows_[i].capture, capture_x, control_y, capture_width, scale(36));
            place(rows_[i].clear, clear_x, control_y + scale(2), clear_width, scale(32));
            place(rows_[i].enabled, toggle_x, control_y + scale(2), toggle_width, scale(32));
        }
        place(status_, 0, section_top - scale(40), width, scale(28), typography_.caption());
        ShowWindow(status_, capture_failed_ ? SW_SHOWNA : SW_HIDE);
        int y = section_top;
        for (std::size_t i = 0; i < sections_.size(); ++i) {
            const int allocated_height = i + 1 == sections_.size()
                ? std::max(section_height, height - y) : section_height;
            sections_[i]->layout({0, y - scroll_, width, y - scroll_ + allocated_height}, dpi_, font_);
            y += allocated_height;
        }
        InvalidateRect(window_, nullptr, FALSE);
    }
    void paint_background(HDC dc) {
        settings_visual_style::erase_background(window_, reinterpret_cast<WPARAM>(dc));
        RECT client{};
        GetClientRect(window_, &client);
        for (std::size_t i = 1; i <= rows_.size(); ++i) {
            const int y = scale(80 + static_cast<int>(i) * (compact_ ? 88 : 56)) - scroll_;
            settings_visual_style::draw_separator(dc, 0, std::min(client.right, static_cast<LONG>(scale(468))), y, dpi_);
        }
    }
    void cancel_capture() {
        if (!capturing_) return;
        keyboard_.end_capture();
        capturing_.reset();
        refresh_rows();
    }
    void reveal_control(HWND control) {
        RECT bounds{}, client{};
        GetWindowRect(control, &bounds);
        MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT*>(&bounds), 2);
        GetClientRect(window_, &client);
        if (bounds.top < 0) scroll_ += bounds.top;
        else if (bounds.bottom > client.bottom) scroll_ += bounds.bottom - client.bottom;
        else return;
        arrange();
    }
    void begin_capture(std::size_t index) {
        cancel_capture();
        if (!rows_[index].draft.active()) return;
        capture_failed_ = false;
        SetWindowTextW(status_, L"");
        capturing_ = index;
        if (!keyboard_.begin_capture([this, index](const KeyboardCaptureResult& result) {
            if (capturing_ != index) return;
            cancel_capture();
            if (result.kind == KeyboardCaptureResultKind::cancelled
                || !rows_[index].draft.active()) return;
            auto& draft = rows_[index].draft;
            auto value = draft.read();
            if (value.binding.gesture == result.gesture) return;
            value.binding.gesture = result.gesture;
            if (!confirm_hotkey_availability(window_, *localization_, value, available_)) return;
            (void)hotkeys_.replace_conflicts(draft.id, value, [&](const auto& conflict) {
                return confirm_hotkey_replacement(window_, *localization_, result.gesture, conflict);
            }, [&] { if (draft.active()) draft.write(value); });
        })) {
            capturing_.reset();
            capture_failed_ = true;
            SetWindowTextW(status_, localization_->text("settings.capture_failed").data());
            if (diagnose_) diagnose_(L"hotkey capture activation failed");
        }
        refresh_rows();
        arrange();
    }
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_COMMAND && HIWORD(wparam) == BN_SETFOCUS) {
            reveal_control(reinterpret_cast<HWND>(lparam));
            return 0;
        }
        if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) >= 100) {
            const auto index = static_cast<std::size_t>((LOWORD(wparam) - 100) / 3);
            const auto action = (LOWORD(wparam) - 100) % 3;
            if (index >= rows_.size() || !rows_[index].draft.active()) return 0;
            if (action == 0) begin_capture(index);
            else {
                cancel_capture();
                auto value = rows_[index].draft.read();
                if (action == 1) value = {};
                else value.enabled = value.binding.gesture
                    && SendMessageW(rows_[index].enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
                rows_[index].draft.write(value);
                hotkeys_.drafts_changed();
            }
            return 0;
        }
        if (message == WM_VSCROLL || message == WM_MOUSEWHEEL) {
            if (message == WM_MOUSEWHEEL) {
                wheel_delta_ += GET_WHEEL_DELTA_WPARAM(wparam);
                scroll_ -= wheel_delta_ / WHEEL_DELTA * scale(48);
                wheel_delta_ %= WHEEL_DELTA;
            }
            else {
                SCROLLINFO info{sizeof(info), SIF_ALL};
                GetScrollInfo(window_, SB_VERT, &info);
                switch (LOWORD(wparam)) {
                case SB_TOP: scroll_ = 0; break;
                case SB_BOTTOM: scroll_ = info.nMax; break;
                case SB_LINEUP: scroll_ -= scale(30); break;
                case SB_LINEDOWN: scroll_ += scale(30); break;
                case SB_PAGEUP: scroll_ -= info.nPage; break;
                case SB_PAGEDOWN: scroll_ += info.nPage; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: scroll_ = info.nTrackPos; break;
                }
            }
            arrange();
            return 0;
        }
        if (message == WM_ERASEBKGND) { paint_background(reinterpret_cast<HDC>(wparam)); return 1; }
        if (message == WM_PAINT) {
            PAINTSTRUCT painting{};
            const auto dc = BeginPaint(window_, &painting);
            paint_background(dc);
            EndPaint(window_, &painting);
            return 0;
        }
        if (message == WM_CTLCOLORSTATIC && reinterpret_cast<HWND>(lparam) == status_)
            return settings_visual_style::handle_secondary_text(wparam, lparam);
        if (settings_visual_style::is_color_message(message)) {
            return settings_visual_style::handle_color_message(message, wparam, lparam);
        }
        if (message == WM_DESTROY) cancel_capture();
        return DefWindowProcW(window_, message, wparam, lparam);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            auto* page = static_cast<HotkeySettingsPage*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            page->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(page));
        }
        auto* page = reinterpret_cast<HotkeySettingsPage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!page) return DefWindowProcW(window, message, wparam, lparam);
        try {
            const auto result = page->message(message, wparam, lparam);
            if (message == WM_NCDESTROY) page->window_ = nullptr;
            return result;
        } catch (...) { return message == WM_CREATE ? -1 : 0; }
    }
    HotkeyRegistry& hotkeys_;
    KeyboardManager& keyboard_;
    std::function<bool(const HotKeyGesture&)> available_;
    std::function<void(std::wstring_view)> diagnose_;
    std::function<void()> changed_;
    const Localization* localization_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr, heading_ = nullptr, status_ = nullptr, section_heading_ = nullptr;
    HWND hotkey_heading_ = nullptr, enabled_heading_ = nullptr;
    std::vector<Row> rows_;
    std::vector<std::unique_ptr<ISettingsPage>> sections_;
    Registration observer_;
    std::optional<std::size_t> capturing_;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    settings_visual_style::PageTypography typography_;
    int scroll_ = 0;
    int wheel_delta_ = 0;
    bool compact_ = false;
    bool capture_failed_ = false;
};
} // namespace

std::unique_ptr<ISettingsPage> make_hotkey_settings_page(
    HotkeyRegistry& hotkeys, KeyboardManager& keyboard,
    std::function<bool(const HotKeyGesture&)> availability,
    std::function<void(std::wstring_view)> diagnose) {
    return std::make_unique<HotkeySettingsPage>(
        hotkeys, keyboard, std::move(availability), std::move(diagnose));
}
} // namespace simpilot
