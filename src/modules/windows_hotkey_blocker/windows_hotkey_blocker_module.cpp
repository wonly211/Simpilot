#include "windows_hotkey_blocker_module.hpp"

#include "settings_visual_style.hpp"
#include "toggle_switch.hpp"

#include <commctrl.h>
#include <algorithm>
#include <format>
#include <stdexcept>

namespace simpilot {
namespace {

constexpr std::size_t lock_index = L'L' - L'A';

class BlockingSettingsPage final : public ISettingsPage {
public:
    BlockingSettingsPage(WindowsHotkeyBlockingSettings& draft, const HotkeyRegistry& hotkeys)
        : draft_(draft), hotkeys_(hotkeys) {}
    ~BlockingSettingsPage() override { if (window_) DestroyWindow(window_); }

    void create(const SettingsPageContext& context) override {
        changed_ = context.changed;
        WNDCLASSW type{
            .lpfnWndProc = procedure,
            .hInstance = context.instance,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
            .lpszClassName = L"Simpilot.WindowsHotkeyBlockingPage",
        };
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Cannot register Windows shortcut settings page");
        }
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 0, 0, context.parent, nullptr,
            context.instance, this);
        if (!window_) throw std::runtime_error("Cannot create Windows shortcut settings page");
        heading_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window_, nullptr, context.instance, nullptr);
        if (!heading_) throw std::runtime_error("Cannot create Windows shortcut label");
        tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
            window_, nullptr, context.instance, nullptr);
        if (tooltip_) SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 480);
        for (std::size_t index = 0; index < toggles_.size(); ++index) {
            if (index == lock_index) continue;
            toggles_[index] = toggle_switch::create(context.instance, window_,
                100 + static_cast<int>(index), L"", draft_.disabled[index], false);
            keys_[index] = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                0, 0, 0, 0, window_, nullptr, context.instance, nullptr);
            descriptions_[index] = CreateWindowW(L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_ENDELLIPSIS,
                0, 0, 0, 0, window_, nullptr, context.instance, nullptr);
            if (!toggles_[index]) throw std::runtime_error("Cannot create Windows shortcut switch");
            SetWindowLongPtrW(toggles_[index], GWL_STYLE,
                GetWindowLongPtrW(toggles_[index], GWL_STYLE) | BS_NOTIFY);
            if (tooltip_) {
                TOOLINFOW tool{sizeof(tool)};
                tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                tool.hwnd = window_;
                tool.uId = reinterpret_cast<UINT_PTR>(toggles_[index]);
                tool.lpszText = LPSTR_TEXTCALLBACKW;
                SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            }
        }
        for (auto& header : headers_) header = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0, window_, nullptr, context.instance, nullptr);
        refresh_language(context.localization);
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        typography_.update(font, dpi);
        if (dpi_ != dpi) scroll_ = MulDiv(scroll_, dpi, dpi_);
        dpi_ = dpi;
        width_ = std::max(1L, bounds.right - bounds.left);
        MoveWindow(window_, bounds.left, bounds.top, bounds.right - bounds.left,
                   bounds.bottom - bounds.top, TRUE);
        SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.title()), TRUE);
        for (auto toggle : toggles_)
            if (toggle) SendMessageW(toggle, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.body()), TRUE);
        for (const auto& labels : {keys_, descriptions_})
            for (auto label : labels) if (label)
                SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.body()), TRUE);
        for (auto header : headers_)
            SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.caption()), TRUE);
        arrange();
    }
    void show(bool visible) override {
        if (visible) refresh_linkage();
        ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
    }
    void refresh_language(const Localization& localization) override {
        localization_ = &localization;
        SetWindowTextW(heading_, localization.text("settings.windows_shortcuts.heading").data());
        for (auto header : headers_)
            SetWindowTextW(header, localization.text("settings.windows_shortcuts.block").data());
        refresh_linkage();
    }

private:
    int scale(int value) const { return MulDiv(value, dpi_, 96); }
    void arrange() {
        RECT client{};
        GetClientRect(window_, &client);
        columns_ = width_ >= scale(704) ? 2 : 1;
        const int rows = (25 + columns_ - 1) / columns_;
        const int content_height = scale(80 + rows * 36 + 4);
        scroll_ = std::clamp(scroll_, 0, std::max(0, content_height - static_cast<int>(client.bottom)));
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS,
            0, content_height - 1, static_cast<UINT>(client.bottom), scroll_};
        SetScrollInfo(window_, SB_VERT, &info, TRUE);
        GetClientRect(window_, &client);
        column_width_ = std::max(1, (static_cast<int>(client.right) - scale(32) * (columns_ - 1)) / columns_);
        MoveWindow(heading_, 0, -scroll_, client.right, scale(36), TRUE);
        for (int i = 0; i < 2; ++i) {
            ShowWindow(headers_[i], i < columns_ ? SW_SHOWNA : SW_HIDE);
            MoveWindow(headers_[i], i * (column_width_ + scale(32)) + column_width_ - scale(44),
                scale(56) - scroll_, scale(44), scale(20), TRUE);
        }
        int visual = 0;
        for (std::size_t i = 0; i < toggles_.size(); ++i) {
            auto toggle = toggles_[i];
            if (!toggle) continue;
            const int x = (visual % columns_) * (column_width_ + scale(32));
            const int y = scale(80 + visual / columns_ * 36 + 2) - scroll_;
            MoveWindow(keys_[i], x, y, scale(64), scale(32), TRUE);
            MoveWindow(descriptions_[i], x + scale(76), y,
                std::max(1, column_width_ - scale(132)), scale(32), TRUE);
            MoveWindow(toggle, x + column_width_ - scale(44), y, scale(44), scale(32), TRUE);
            ++visual;
        }
        InvalidateRect(window_, nullptr, TRUE);
    }
    void reveal(HWND control) {
        RECT bounds{}, client{};
        GetWindowRect(control, &bounds);
        MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT*>(&bounds), 2);
        GetClientRect(window_, &client);
        if (bounds.top < 0) scroll_ += bounds.top;
        else if (bounds.bottom > client.bottom) scroll_ += bounds.bottom - client.bottom;
        else return;
        arrange();
    }
    void draw_separators(HDC dc) const {
        for (int visual = 0; visual < 25; ++visual) {
            const int x = (visual % columns_) * (column_width_ + scale(32));
            settings_visual_style::draw_separator(dc, x, x + column_width_,
                scale(80 + (visual / columns_ + 1) * 36) - scroll_, dpi_);
        }
    }
    void refresh_linkage() {
        if (!localization_) return;
        for (std::size_t index = 0; index < toggles_.size(); ++index) {
            if (!toggles_[index]) continue;
            const auto required = hotkeys_.draft_requires_windows_blocking(index);
            auto letter = static_cast<wchar_t>(L'A' + index);
            const auto key = "settings.windows_shortcuts." + std::string(1, static_cast<char>('a' + index));
            auto description = localization_->text(key);
            auto label = std::vformat(localization_->text("settings.windows_shortcuts.label"),
                std::make_wformat_args(letter, description));
            if (required) label += localization_->text("settings.windows_shortcuts.linked_suffix");
            SetWindowTextW(toggles_[index], label.c_str());
            const auto gesture = L"Win+" + std::wstring(1, letter);
            SetWindowTextW(keys_[index], gesture.c_str());
            SetWindowTextW(descriptions_[index], std::wstring(description).c_str());
            EnableWindow(keys_[index], !required);
            EnableWindow(descriptions_[index], !required);
            SendMessageW(toggles_[index], BM_SETCHECK,
                draft_.disabled[index] || required ? BST_CHECKED : BST_UNCHECKED, 0);
            EnableWindow(toggles_[index], required ? FALSE : TRUE);
        }
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
            static_cast<BlockingSettingsPage*>(creation->lpCreateParams)->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* page = reinterpret_cast<BlockingSettingsPage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (page) {
            try {
                if (message == WM_PAINT) {
                    PAINTSTRUCT painting{};
                    const auto dc = BeginPaint(window, &painting);
                    page->draw_separators(dc);
                    EndPaint(window, &painting);
                    return 0;
                }
                if (message == WM_PRINTCLIENT) {
                    settings_visual_style::erase_background(window, wparam);
                    page->draw_separators(reinterpret_cast<HDC>(wparam));
                    return 0;
                }
                if (message == WM_VSCROLL) {
                    SCROLLINFO info{sizeof(info), SIF_ALL};
                    GetScrollInfo(window, SB_VERT, &info);
                    switch (LOWORD(wparam)) {
                    case SB_TOP: page->scroll_ = 0; break;
                    case SB_BOTTOM: page->scroll_ = info.nMax; break;
                    case SB_LINEUP: page->scroll_ -= page->scale(36); break;
                    case SB_LINEDOWN: page->scroll_ += page->scale(36); break;
                    case SB_PAGEUP: page->scroll_ -= info.nPage; break;
                    case SB_PAGEDOWN: page->scroll_ += info.nPage; break;
                    case SB_THUMBTRACK:
                    case SB_THUMBPOSITION: page->scroll_ = info.nTrackPos; break;
                    default: return 0;
                    }
                    page->arrange();
                    return 0;
                }
                if (message == WM_MOUSEWHEEL) {
                    page->wheel_delta_ += GET_WHEEL_DELTA_WPARAM(wparam);
                    const int steps = page->wheel_delta_ / WHEEL_DELTA;
                    page->wheel_delta_ %= WHEEL_DELTA;
                    page->scroll_ -= steps * page->scale(36 * 3);
                    page->arrange();
                    return 0;
                }
                if (message == WM_COMMAND && HIWORD(wparam) == BN_SETFOCUS) {
                    page->reveal(reinterpret_cast<HWND>(lparam));
                    return 0;
                }
                if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lparam)->hwndFrom == page->tooltip_
                    && reinterpret_cast<NMHDR*>(lparam)->code == TTN_GETDISPINFOW) {
                    auto* info = reinterpret_cast<NMTTDISPINFOW*>(lparam);
                    const auto control = reinterpret_cast<HWND>(info->hdr.idFrom);
                    const auto length = GetWindowTextLengthW(control);
                    page->tooltip_text_.resize(static_cast<std::size_t>(length) + 1);
                    GetWindowTextW(control, page->tooltip_text_.data(), length + 1);
                    info->lpszText = page->tooltip_text_.data();
                    return 0;
                }
                if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) {
                    const auto id = LOWORD(wparam);
                    if (id >= 100 && id < 126 && id - 100 != lock_index) {
                        const auto index = static_cast<std::size_t>(id - 100);
                        if (!page->hotkeys_.draft_requires_windows_blocking(index)) {
                            page->draft_.disabled[index] =
                                SendMessageW(page->toggles_[index], BM_GETCHECK, 0, 0) == BST_CHECKED;
                            if (page->changed_) page->changed_();
                        }
                        return 0;
                    }
                }
                if (message == WM_ERASEBKGND) {
                    return settings_visual_style::erase_background(window, wparam);
                }
                if (settings_visual_style::is_color_message(message)) {
                    return settings_visual_style::handle_color_message(message, wparam, lparam);
                }
            } catch (...) { return 0; }
            if (message == WM_NCDESTROY) page->window_ = nullptr;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    WindowsHotkeyBlockingSettings& draft_;
    const HotkeyRegistry& hotkeys_;
    const Localization* localization_ = nullptr;
    std::function<void()> changed_;
    HWND window_ = nullptr;
    HWND heading_ = nullptr;
    HWND tooltip_ = nullptr;
    std::wstring tooltip_text_;
    settings_visual_style::PageTypography typography_;
    UINT dpi_ = 96;
    int width_ = 0, scroll_ = 0, wheel_delta_ = 0, columns_ = 2, column_width_ = 0;
    std::array<HWND, 26> toggles_{};
    std::array<HWND, 26> keys_{}, descriptions_{};
    std::array<HWND, 2> headers_{};
};

class WindowsHotkeyBlockingModule final : public IAppModule {
public:
    WindowsHotkeyBlockingModule(WindowsHotkeyBlockingSettings settings,
        SettingsRegistry& pages, SettingsParticipantRegistry& participants,
        HotkeyRegistry& hotkeys, std::function<bool(const std::array<bool, 26>&)> apply)
        : live_(settings), draft_(settings), pages_(pages), participants_(participants),
          hotkeys_(hotkeys), apply_(std::move(apply)) {}
    ~WindowsHotkeyBlockingModule() override { stop(); }
    void start() override {
        if (started_) return;
        started_ = true;
        if (!apply_(live_.disabled)) throw std::runtime_error("Cannot apply Windows shortcut policy");
        participant_ = participants_.add("windows_hotkey_blocker", 80, SettingsParticipant{
            .begin = [this] { draft_ = live_; },
            .dirty = [this] { return draft_ != live_; },
            .apply = [this] { return apply_(draft_.disabled); },
            .write = [this](SettingsDocument& document) { draft_.write(document); },
            .rollback = [this] { return apply_(live_.disabled); },
            .finish = [this] { live_ = draft_; },
            .cancel = [this] { draft_ = live_; }});
        page_ = pages_.add("windows_hotkey_blocker", 80, SettingsPageContribution{
            "settings.tab.windows_shortcuts", [this] {
                return std::make_unique<BlockingSettingsPage>(draft_, hotkeys_);
            }});
    }
    void stop() noexcept override {
        page_.reset();
        participant_.reset();
        if (started_) {
            try { (void)apply_({}); } catch (...) {}
        }
        started_ = false;
    }
private:
    WindowsHotkeyBlockingSettings live_;
    WindowsHotkeyBlockingSettings draft_;
    SettingsRegistry& pages_;
    SettingsParticipantRegistry& participants_;
    HotkeyRegistry& hotkeys_;
    std::function<bool(const std::array<bool, 26>&)> apply_;
    Registration page_;
    Registration participant_;
    bool started_ = false;
};

} // namespace

WindowsHotkeyBlockingSettings WindowsHotkeyBlockingSettings::read(const SettingsDocument& document) {
    WindowsHotkeyBlockingSettings result;
    for (auto character : document.get(L"DisabledWindowsHotkeys").value_or(L"")) {
        if (character >= L'a' && character <= L'z') character -= L'a' - L'A';
        if (character >= L'A' && character <= L'Z' && character != L'L') {
            result.disabled[static_cast<std::size_t>(character - L'A')] = true;
        }
    }
    return result;
}

void WindowsHotkeyBlockingSettings::write(SettingsDocument& document) const {
    std::wstring value;
    for (std::size_t index = 0; index < disabled.size(); ++index) {
        if (index != lock_index && disabled[index]) value.push_back(static_cast<wchar_t>(L'A' + index));
    }
    document.set(L"WindowsHotkeys", L"DisabledWindowsHotkeys", value);
}

std::unique_ptr<IAppModule> make_windows_hotkey_blocker_module(
    const SettingsDocument& document, SettingsRegistry& pages,
    SettingsParticipantRegistry& participants, HotkeyRegistry& hotkeys,
    std::function<bool(const std::array<bool, 26>&)> apply_policy) {
    return std::make_unique<WindowsHotkeyBlockingModule>(
        WindowsHotkeyBlockingSettings::read(document), pages, participants,
        hotkeys, std::move(apply_policy));
}

} // namespace simpilot
