#include "windows_hotkey_blocker_module.hpp"

#include "settings_visual_style.hpp"
#include "toggle_switch.hpp"

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
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, context.parent, nullptr,
            context.instance, this);
        if (!window_) throw std::runtime_error("Cannot create Windows shortcut settings page");
        for (auto* control : {&heading_, &scope_, &runtime_}) {
            *control = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, window_, nullptr, context.instance, nullptr);
            if (!*control) throw std::runtime_error("Cannot create Windows shortcut label");
        }
        for (std::size_t index = 0; index < toggles_.size(); ++index) {
            if (index == lock_index) continue;
            toggles_[index] = toggle_switch::create(context.instance, window_,
                100 + static_cast<int>(index), L"", draft_.disabled[index], true);
            if (!toggles_[index]) throw std::runtime_error("Cannot create Windows shortcut switch");
        }
        refresh_language(context.localization);
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        MoveWindow(window_, bounds.left, bounds.top, bounds.right - bounds.left,
                   bounds.bottom - bounds.top, TRUE);
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        const auto width = std::max(1L, bounds.right - bounds.left);
        const auto height = std::max(1L, bounds.bottom - bounds.top);
        const auto columns = width >= scale(850) ? 3 : 2;
        const auto rows = (25 + columns - 1) / columns;
        const auto top = scale(100);
        const auto row_height = std::min(scale(32), std::max(scale(20),
            (static_cast<int>(height) - top - scale(64)) / rows));
        const auto gap = scale(16);
        const auto column_width = (width - gap * (columns - 1)) / columns;
        MoveWindow(heading_, 0, 0, width, scale(32), TRUE);
        MoveWindow(scope_, 0, scale(38), width, scale(56), TRUE);
        int visual = 0;
        for (std::size_t index = 0; index < toggles_.size(); ++index) {
            if (!toggles_[index]) continue;
            MoveWindow(toggles_[index], (visual % columns) * (column_width + gap),
                top + (visual / columns) * row_height, column_width, row_height, TRUE);
            SendMessageW(toggles_[index], WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            ++visual;
        }
        MoveWindow(runtime_, 0, top + rows * row_height + scale(8),
            width, scale(56), TRUE);
        for (auto control : {heading_, scope_, runtime_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }
    void show(bool visible) override {
        if (visible) refresh_linkage();
        ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
    }
    void refresh_language(const Localization& localization) override {
        localization_ = &localization;
        SetWindowTextW(heading_, localization.text("settings.windows_shortcuts.heading").data());
        SetWindowTextW(scope_, localization.text("settings.windows_shortcuts.scope").data());
        SetWindowTextW(runtime_, localization.text("settings.windows_shortcuts.runtime").data());
        refresh_linkage();
    }

private:
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
            SendMessageW(toggles_[index], BM_SETCHECK,
                draft_.disabled[index] || required ? BST_CHECKED : BST_UNCHECKED, 0);
            EnableWindow(toggles_[index], required ? FALSE : TRUE);
        }
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* page = reinterpret_cast<BlockingSettingsPage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (page) {
            try {
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
                if (message == WM_CTLCOLORSTATIC
                    && (reinterpret_cast<HWND>(lparam) == page->scope_
                        || reinterpret_cast<HWND>(lparam) == page->runtime_)) {
                    return settings_visual_style::handle_secondary_text(wparam, lparam);
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
    HWND scope_ = nullptr;
    HWND runtime_ = nullptr;
    std::array<HWND, 26> toggles_{};
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
