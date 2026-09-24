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
        constexpr std::string_view keys[]{"settings.global_hotkeys.column.function",
            "settings.global_hotkeys.column.hotkey", "settings.global_hotkeys.column.command",
            "settings.global_hotkeys.column.enabled"};
        for (std::size_t i = 0; i < headers_.size(); ++i) {
            headers_[i] = control(L"STATIC", 0, 0);
            header_keys_[i] = keys[i];
        }
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
        bounds_ = bounds;
        dpi_ = dpi;
        font_ = font;
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
        for (std::size_t i = 0; i < headers_.size(); ++i) {
            SetWindowTextW(headers_[i], localization.text(header_keys_[i]).data());
        }
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
        rows_.push_back({draft, control(L"STATIC", 0, SS_CENTERIMAGE),
            control(L"BUTTON", id, WS_TABSTOP | BS_PUSHBUTTON),
            control(L"BUTTON", id + 1, WS_TABSTOP | BS_PUSHBUTTON),
            toggle_switch::create(instance_, window_, id + 2, L"", false)});
    }
    void refresh_rows() {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            auto& row = rows_[i];
            const auto active = row.draft.active();
            const auto value = active ? row.draft.read() : BuiltInHotKey{};
            SetWindowTextW(row.label, localization_->text(row.draft.label_key).data());
            SetWindowTextW(row.clear, localization_->text("settings.clear").data());
            const auto title = hotkey_capture_button_text(value.binding.gesture,
                capturing_ == i, *localization_);
            SetWindowTextW(row.capture, title.c_str());
            SetWindowTextW(row.enabled, localization_->text(row.draft.label_key).data());
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
        const int width = std::max(1L, client.right);
        const int height = std::max(1L, client.bottom);
        const int section_height = scale(250);
        const int content_height = scale(110 + static_cast<int>(rows_.size()) * 48)
            + static_cast<int>(sections_.size()) * section_height;
        scroll_ = std::clamp(scroll_, 0, std::max(0, content_height - height));
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS,
            0, content_height - 1, static_cast<UINT>(height), scroll_, 0};
        SetScrollInfo(window_, SB_VERT, &info, TRUE);
        const int toggle_width = std::min(scale(64), width / 6);
        const int clear_width = std::min(scale(90), width / 5);
        const int label_width = width * 30 / 100;
        const int gap = scale(8);
        const int toggle_x = width - toggle_width;
        const int clear_x = toggle_x - clear_width - gap;
        const int capture_width = std::max(1, clear_x - label_width - gap * 2);
        auto place = [&](HWND control, int x, int y, int w, int h) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            MoveWindow(control, x, y - scroll_, w, h, TRUE);
        };
        place(heading_, 0, 0, width, scale(30));
        const int xs[]{0, label_width, clear_x, toggle_x};
        const int widths[]{label_width - gap, capture_width, clear_width, toggle_width};
        for (int i = 0; i < 4; ++i) place(headers_[i], xs[i], scale(38), widths[i], scale(28));
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const int y = scale(72 + static_cast<int>(i) * 48);
            const HWND controls[]{rows_[i].label, rows_[i].capture, rows_[i].clear, rows_[i].enabled};
            for (int j = 0; j < 4; ++j) place(controls[j], xs[j], y, widths[j], scale(34));
        }
        int y = scale(76 + static_cast<int>(rows_.size()) * 48);
        place(status_, 0, y, width, scale(28));
        y += scale(34);
        for (auto& section : sections_) {
            section->layout({0, y - scroll_, width, y - scroll_ + section_height}, dpi_, font_);
            y += section_height;
        }
    }
    void cancel_capture() {
        if (!capturing_) return;
        keyboard_.end_capture();
        capturing_.reset();
        refresh_rows();
    }
    void begin_capture(std::size_t index) {
        cancel_capture();
        if (!rows_[index].draft.active()) return;
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
            SetWindowTextW(status_, localization_->text("settings.capture_failed").data());
            if (diagnose_) diagnose_(L"hotkey capture activation failed");
        }
        refresh_rows();
    }
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) {
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
            if (message == WM_MOUSEWHEEL) scroll_ -= GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * scale(48);
            else {
                SCROLLINFO info{sizeof(info), SIF_ALL};
                GetScrollInfo(window_, SB_VERT, &info);
                switch (LOWORD(wparam)) {
                case SB_LINEUP: scroll_ -= scale(30); break;
                case SB_LINEDOWN: scroll_ += scale(30); break;
                case SB_PAGEUP: scroll_ -= info.nPage; break;
                case SB_PAGEDOWN: scroll_ += info.nPage; break;
                case SB_THUMBTRACK: scroll_ = info.nTrackPos; break;
                }
            }
            arrange();
            return 0;
        }
        if (message == WM_ERASEBKGND) return settings_visual_style::erase_background(window_, wparam);
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
    HWND window_ = nullptr, heading_ = nullptr, status_ = nullptr;
    std::array<HWND, 4> headers_{};
    std::array<std::string_view, 4> header_keys_{};
    std::vector<Row> rows_;
    std::vector<std::unique_ptr<ISettingsPage>> sections_;
    Registration observer_;
    std::optional<std::size_t> capturing_;
    RECT bounds_{};
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    int scroll_ = 0;
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
