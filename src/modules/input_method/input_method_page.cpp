#include "input_method_page.hpp"

#include "settings_visual_style.hpp"
#include "toggle_switch.hpp"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>

namespace simpilot::input_method {
namespace {

constexpr int enable_id = 1, add_id = 2, edit_id = 3, remove_id = 4, refresh_id = 5;
constexpr int list_id = 6, path_id = 7, status_id = 8;
constexpr int rule_path_id = 101, policy_id = 102, profile_id = 103, mode_id = 104;
constexpr int browse_id = 105;

HMENU identifier(int id) { return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)); }
void text(HWND window, std::wstring_view value) {
    SetWindowTextW(window, std::wstring(value).c_str());
}
void font(HWND window, HFONT value) {
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(value), TRUE);
}
std::wstring read_text(HWND window) {
    std::wstring result(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    GetWindowTextW(window, result.data(), static_cast<int>(result.size()));
    result.resize(wcslen(result.c_str()));
    return result;
}
const char* policy_key(Policy policy) {
    return policy == Policy::fixed ? "settings.input_method.fixed"
         : policy == Policy::ignore ? "settings.input_method.ignore"
                                   : "settings.input_method.remember";
}
const char* mode_key(Mode mode) {
    return mode == Mode::chinese ? "settings.input_method.chinese"
                                : "settings.input_method.english";
}

class RuleDialog final {
public:
    RuleDialog(HINSTANCE instance, HWND owner, const Localization& localization,
        Rule rule, std::vector<Profile> profiles, const std::vector<Rule>& others,
        ApplicationSelectionServices applications)
        : instance_(instance), owner_(owner), localization_(localization),
          rule_(std::move(rule)), profiles_(std::move(profiles)), others_(others),
          applications_(std::move(applications)) {}
    ~RuleDialog() { if (window_) DestroyWindow(window_); }

    std::optional<Rule> run() {
        const WNDCLASSW klass{
            .lpfnWndProc = procedure, .hInstance = instance_,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .lpszClassName = L"Simpilot.InputMethodRuleDialog"};
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return std::nullopt;
        dpi_ = owner_ ? GetDpiForWindow(owner_) : GetDpiForSystem();
        window_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            klass.lpszClassName, localization_.text("settings.input_method.dialog_edit").data(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, owner_, nullptr, instance_, this);
        if (!window_) return std::nullopt;
        create_controls();
        adjust_size();
        const bool owner_enabled = owner_ && IsWindowEnabled(owner_);
        if (owner_enabled) EnableWindow(owner_, FALSE);
        ShowWindow(window_, SW_SHOW);
        SetFocus(policy_);
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
        if (owner_enabled) EnableWindow(owner_, TRUE);
        if (owner_) SetActiveWindow(owner_);
        if (quit) PostQuitMessage(quit_code);
        return result_;
    }

private:
    HWND control(const wchar_t* klass, int id, DWORD style = 0, DWORD exstyle = 0) {
        auto window = CreateWindowExW(exstyle, klass, L"",
            WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
            window_, identifier(id), instance_, nullptr);
        if (!window) throw std::runtime_error("Cannot create input method rule control");
        return window;
    }

    void create_controls() {
        heading_ = control(L"STATIC", 110);
        path_label_ = control(L"STATIC", 111);
        path_ = control(L"EDIT", rule_path_id,
            WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
        browse_ = control(L"BUTTON", browse_id, WS_TABSTOP);
        policy_label_ = control(L"STATIC", 112);
        policy_ = control(L"COMBOBOX", policy_id, WS_TABSTOP | CBS_DROPDOWNLIST);
        profile_label_ = control(L"STATIC", 113);
        profile_ = control(L"COMBOBOX", profile_id, WS_TABSTOP | CBS_DROPDOWNLIST);
        mode_label_ = control(L"STATIC", 114);
        mode_ = control(L"COMBOBOX", mode_id, WS_TABSTOP | CBS_DROPDOWNLIST);
        error_ = control(L"STATIC", 115);
        save_ = control(L"BUTTON", IDOK, WS_TABSTOP | BS_DEFPUSHBUTTON);
        cancel_ = control(L"BUTTON", IDCANCEL, WS_TABSTOP);
        text(heading_, localization_.text("settings.input_method.dialog_edit"));
        text(path_label_, localization_.text("settings.input_method.path"));
        text(path_, rule_.executable_path);
        text(policy_label_, localization_.text("settings.input_method.behavior"));
        text(profile_label_, localization_.text("settings.input_method.select_profile"));
        text(mode_label_, localization_.text("settings.input_method.select_mode"));
        text(save_, localization_.text("settings.save"));
        text(cancel_, localization_.text("settings.cancel"));
        text(browse_, L"...");
        settings_visual_style::style_button(browse_);
        settings_visual_style::style_button(save_, settings_visual_style::ButtonStyle::primary);
        settings_visual_style::style_button(cancel_, settings_visual_style::ButtonStyle::quiet);
        settings_visual_style::set_button_tooltip(
            browse_, localization_.text("settings.input_method.browse"));
        for (const auto policy : {Policy::remember, Policy::fixed, Policy::ignore})
            SendMessageW(policy_, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(localization_.text(policy_key(policy)).data()));
        SendMessageW(policy_, CB_SETCURSEL, static_cast<WPARAM>(rule_.policy), 0);
        // Keep an unavailable saved identifier visible and selected. Opening
        // and saving a rule must not silently replace it with another IME.
        if (!rule_.profile_id.empty() && !find_profile(profiles_, rule_.profile_id)) {
            profiles_.push_back({rule_.profile_id,
                std::wstring(localization_.text("settings.input_method.unavailable_profile")),
                nullptr, true, true, false});
        }
        int selected = -1;
        for (std::size_t index = 0; index < profiles_.size(); ++index) {
            const auto& profile = profiles_[index];
            const auto item = SendMessageW(profile_, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(profile.name.c_str()));
            SendMessageW(profile_, CB_SETITEMDATA, item, static_cast<LPARAM>(index));
            if (profile.id == rule_.profile_id) selected = static_cast<int>(item);
        }
        if (selected < 0 && !profiles_.empty()) selected = 0;
        SendMessageW(profile_, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
        refresh_modes(rule_.mode);
        update_fonts();
    }

    const Profile* selected_profile() const {
        const auto index = SendMessageW(profile_, CB_GETCURSEL, 0, 0);
        if (index == CB_ERR) return nullptr;
        const auto data = SendMessageW(profile_, CB_GETITEMDATA, index, 0);
        if (data < 0 || static_cast<std::size_t>(data) >= profiles_.size()) return nullptr;
        return &profiles_[static_cast<std::size_t>(data)];
    }

    void refresh_modes(Mode desired) {
        SendMessageW(mode_, CB_RESETCONTENT, 0, 0);
        const auto* profile = selected_profile();
        if (profile && profile->supports_chinese) {
            const auto item = SendMessageW(mode_, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(localization_.text(mode_key(Mode::chinese)).data()));
            SendMessageW(mode_, CB_SETITEMDATA, item, static_cast<LPARAM>(Mode::chinese));
        }
        const auto english = SendMessageW(mode_, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(localization_.text(mode_key(Mode::english)).data()));
        SendMessageW(mode_, CB_SETITEMDATA, english, static_cast<LPARAM>(Mode::english));
        SendMessageW(mode_, CB_SETCURSEL,
            desired == Mode::chinese && profile && profile->supports_chinese ? 0 : english, 0);
    }

    bool fixed() const {
        return SendMessageW(policy_, CB_GETCURSEL, 0, 0) == static_cast<int>(Policy::fixed);
    }
    int scale(int value) const { return MulDiv(value, dpi_, 96); }

    void update_fonts() {
        typography_.update(nullptr, dpi_);
        for (const auto control : {heading_, path_label_, path_, browse_, policy_label_,
                policy_, profile_label_, profile_, mode_label_, mode_, error_, save_, cancel_})
            font(control, typography_.body());
        font(heading_, typography_.title());
        font(error_, typography_.caption());
        for (const auto combo : {policy_, profile_, mode_})
            SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), scale(22));
    }

    void adjust_size() {
        RECT outer{0, 0, scale(640), scale(fixed() ? 476 : 328)};
        AdjustWindowRectExForDpi(&outer,
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            FALSE, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, dpi_);
        MONITORINFO monitor{.cbSize = sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &monitor);
        const auto width = outer.right - outer.left;
        const auto height = outer.bottom - outer.top;
        const int x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
        const int y = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
        SetWindowPos(window_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
    }

    void layout() {
        if (!path_) return;
        RECT bounds{};
        GetClientRect(window_, &bounds);
        const int left = scale(28), width = bounds.right - scale(56);
        MoveWindow(heading_, left, scale(20), width, scale(32), TRUE);
        MoveWindow(path_label_, left, scale(72), width, scale(20), TRUE);
        MoveWindow(path_, left, scale(100), width - scale(44), scale(32), TRUE);
        MoveWindow(browse_, bounds.right - left - scale(32), scale(100), scale(32), scale(32), TRUE);
        MoveWindow(policy_label_, left, scale(148), width, scale(20), TRUE);
        MoveWindow(policy_, left, scale(176), width, scale(220), TRUE);
        for (const auto child : {profile_label_, profile_, mode_label_, mode_}) {
            ShowWindow(child, fixed() ? SW_SHOW : SW_HIDE);
            EnableWindow(child, fixed());
        }
        MoveWindow(profile_label_, left, scale(220), width, scale(20), TRUE);
        MoveWindow(profile_, left, scale(248), width, scale(220), TRUE);
        MoveWindow(mode_label_, left, scale(292), width, scale(20), TRUE);
        MoveWindow(mode_, left, scale(320), scale(192), scale(220), TRUE);
        const int footer = bounds.bottom - scale(72);
        MoveWindow(error_, left, footer - scale(40), width, scale(36), TRUE);
        MoveWindow(cancel_, bounds.right - left - scale(184), footer + scale(18), scale(88), scale(36), TRUE);
        MoveWindow(save_, bounds.right - left - scale(88), footer + scale(18), scale(88), scale(36), TRUE);
        InvalidateRect(window_, nullptr, TRUE);
    }

    void accept() {
        Rule candidate = rule_;
        candidate.executable_path = read_text(path_);
        candidate.policy = static_cast<Policy>(SendMessageW(policy_, CB_GETCURSEL, 0, 0));
        if (fixed()) {
            const auto* profile = selected_profile();
            if (!profile) {
                text(error_, localization_.text("settings.input_method.invalid_rule"));
                return;
            }
            candidate.profile_id = profile->id;
            const auto index = SendMessageW(mode_, CB_GETCURSEL, 0, 0);
            candidate.mode = static_cast<Mode>(SendMessageW(mode_, CB_GETITEMDATA, index, 0));
        } else {
            candidate.profile_id.clear();
            candidate.mode = Mode::english;
        }
        Settings check{false, others_};
        check.rules.push_back(candidate);
        if (!validate(check)) {
            text(error_, localization_.text("settings.input_method.duplicate_rule"));
            return;
        }
        result_ = std::move(candidate);
        DestroyWindow(window_);
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            auto* self = static_cast<RuleDialog*>(
                reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto* self = reinterpret_cast<RuleDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return DefWindowProcW(window, message, wparam, lparam);
        try {
            if (message == WM_COMMAND) {
                const auto id = LOWORD(wparam), notification = HIWORD(wparam);
                if (id == IDCANCEL) DestroyWindow(window);
                else if (id == IDOK) self->accept();
                else if (id == browse_id && notification == BN_CLICKED) {
                    if (const auto selected = choose_application(self->instance_, window,
                            self->browse_, self->localization_, self->applications_))
                        text(self->path_, *selected);
                } else if (id == policy_id && notification == CBN_SELCHANGE) {
                    text(self->error_, L"");
                    self->adjust_size();
                } else if (id == profile_id && notification == CBN_SELCHANGE) {
                    self->refresh_modes(Mode::english);
                }
                return 0;
            }
            if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
            if (message == WM_SIZE) { self->layout(); return 0; }
            if (message == WM_DPICHANGED) {
                self->dpi_ = HIWORD(wparam);
                self->update_fonts();
                self->adjust_size();
                return 0;
            }
            if (message == WM_PAINT) {
                PAINTSTRUCT paint{};
                auto dc = BeginPaint(window, &paint);
                RECT client{};
                GetClientRect(window, &client);
                settings_visual_style::draw_separator(dc, 0, client.right,
                    client.bottom - self->scale(72), self->dpi_);
                EndPaint(window, &paint);
                return 0;
            }
            if (message == WM_ERASEBKGND)
                return settings_visual_style::erase_background(window, wparam);
            if (settings_visual_style::is_color_message(message))
                return settings_visual_style::handle_color_message(message, wparam, lparam);
        } catch (...) {
            if (self->error_) text(self->error_, self->localization_.text(
                "settings.input_method.state_failed"));
        }
        if (message == WM_NCDESTROY) self->window_ = nullptr;
        return DefWindowProcW(window, message, wparam, lparam);
    }

    HINSTANCE instance_;
    HWND owner_, window_ = nullptr;
    const Localization& localization_;
    Rule rule_;
    std::vector<Profile> profiles_;
    std::vector<Rule> others_;
    ApplicationSelectionServices applications_;
    std::optional<Rule> result_;
    UINT dpi_ = 96;
    settings_visual_style::PageTypography typography_;
    HWND heading_{}, path_label_{}, path_{}, browse_{}, policy_label_{}, policy_{};
    HWND profile_label_{}, profile_{}, mode_label_{}, mode_{}, error_{}, save_{}, cancel_{};
};

class Page final : public ISettingsPage {
public:
    Page(Settings& draft, PageServices services) : draft_(draft), services_(std::move(services)) {}
    ~Page() override { if (window_) DestroyWindow(window_); }

    void create(const SettingsPageContext& context) override {
        instance_ = context.instance;
        localization_ = &context.localization;
        changed_ = context.changed;
        const WNDCLASSW klass{
            .lpfnWndProc = procedure, .hInstance = instance_,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .lpszClassName = L"Simpilot.InputMethodSettingsPage"};
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Cannot register input method settings page");
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, klass.lpszClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, context.parent, nullptr, instance_, this);
        if (!window_) throw std::runtime_error("Cannot create input method settings page");
        heading_ = control(L"STATIC", 10);
        label_ = control(L"STATIC", 11);
        toggle_ = toggle_switch::create(instance_, window_, enable_id, L"", draft_.enabled);
        add_ = control(L"BUTTON", add_id, WS_TABSTOP);
        edit_ = control(L"BUTTON", edit_id, WS_TABSTOP);
        remove_ = control(L"BUTTON", remove_id, WS_TABSTOP);
        refresh_ = control(L"BUTTON", refresh_id, WS_TABSTOP);
        list_ = control(WC_LISTVIEWW, list_id, WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL
            | LVS_SHOWSELALWAYS);
        path_ = control(L"EDIT", path_id, WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL);
        status_ = control(L"STATIC", status_id);
        if (!toggle_) throw std::runtime_error("Cannot create input method toggle");
        ListView_SetExtendedListViewStyle(list_,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_INFOTIP);
        settings_visual_style::style_list_view(list_);
        settings_visual_style::style_button(add_, settings_visual_style::ButtonStyle::add);
        settings_visual_style::style_button(edit_, settings_visual_style::ButtonStyle::edit);
        settings_visual_style::style_button(remove_, settings_visual_style::ButtonStyle::remove);
        settings_visual_style::style_button(refresh_);
        refresh_language(context.localization);
        layout({0, 0, 650, 440}, context.dpi, context.font);
    }

    void layout(RECT bounds, UINT dpi, HFONT body) override {
        dpi_ = dpi;
        typography_.update(body, dpi);
        for (const auto child : {heading_, label_, toggle_, add_, edit_, remove_,
                refresh_, list_, path_, status_}) font(child, typography_.body());
        font(heading_, typography_.title());
        font(status_, typography_.caption());
        const int width = std::max(1L, bounds.right - bounds.left);
        const int height = std::max(1L, bounds.bottom - bounds.top);
        MoveWindow(window_, bounds.left, bounds.top, width, height, TRUE);
        MoveWindow(heading_, 0, 0, width, scale(36), TRUE);
        const int row_width = std::min(width, scale(640));
        MoveWindow(label_, 0, scale(64), row_width - scale(72), scale(40), TRUE);
        MoveWindow(toggle_, row_width - scale(44), scale(64), scale(44), scale(32), TRUE);
        const int y = scale(116);
        MoveWindow(add_, 0, y, scale(32), scale(32), TRUE);
        MoveWindow(edit_, scale(40), y, scale(32), scale(32), TRUE);
        MoveWindow(remove_, scale(80), y, scale(32), scale(32), TRUE);
        MoveWindow(refresh_, std::max(scale(124), width - scale(180)),
            y, scale(180), scale(32), TRUE);
        const int list_y = y + scale(44);
        const int list_bottom = std::max(list_y + scale(40), height - scale(72));
        MoveWindow(list_, 0, list_y, width, list_bottom - list_y, TRUE);
        MoveWindow(path_, 0, list_bottom + scale(8), width, scale(28), TRUE);
        MoveWindow(status_, 0, list_bottom + scale(44), width, scale(24), TRUE);
        update_columns(width);
    }

    void show(bool visible) override {
        ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
        if (visible) {
            refresh_rows();
            update_status();
            SetTimer(window_, 1, 500, nullptr);
        } else KillTimer(window_, 1);
    }

    void refresh_language(const Localization& localization) override {
        localization_ = &localization;
        text(heading_, localization.text("settings.input_method.heading"));
        text(label_, localization.text("settings.input_method.enable"));
        text(toggle_, localization.text("settings.input_method.enable"));
        text(path_, L"");
        for (const auto [control, key] : {
                std::pair{add_, "settings.input_method.add"},
                {edit_, "settings.input_method.edit"}, {remove_, "settings.input_method.remove"},
                {refresh_, "settings.input_method.refresh"}}) {
            text(control, localization.text(key));
            settings_visual_style::set_button_tooltip(control, localization.text(key));
        }
        RECT client{};
        GetClientRect(window_, &client);
        update_columns(client.right);
        refresh_rows();
        update_status();
    }

private:
    HWND control(const wchar_t* klass, int id, DWORD style = 0) {
        const auto window = CreateWindowExW(0, klass, L"", WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0, window_, identifier(id), instance_, nullptr);
        if (!window) throw std::runtime_error("Cannot create input method setting control");
        return window;
    }
    int scale(int value) const { return MulDiv(value, dpi_, 96); }
    std::wstring_view localized(const char* key) const { return localization_->text(key); }

    void update_columns(int width) {
        constexpr std::array<const char*, 5> keys{
            "settings.input_method.application", "settings.input_method.policy",
            "settings.input_method.profile", "settings.input_method.mode",
            "settings.input_method.status"};
        const auto columns = Header_GetItemCount(ListView_GetHeader(list_));
        const int scrollbar = GetSystemMetricsForDpi(
            SM_CXVSCROLL, std::max(dpi_, GetDpiForWindow(list_)));
        const int available = width - scrollbar;
        const std::array<int, 5> widths{
            std::max(scale(144), available - scale(476)),
            scale(144), scale(156), scale(72), scale(104)};
        for (int index = 0; index < 5; ++index) {
            std::wstring name(localized(keys[index]));
            LVCOLUMNW column{
                .mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM,
                .cx = widths[index], .pszText = name.data(), .iSubItem = index};
            if (index < columns) ListView_SetColumn(list_, index, &column);
            else ListView_InsertColumn(list_, index, &column);
        }
    }

    void refresh_rows() {
        const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        ListView_DeleteAllItems(list_);
        const auto& profiles = services_.profiles();
        for (std::size_t index = 0; index < draft_.rules.size(); ++index) {
            const auto& rule = draft_.rules[index];
            std::wstring path = rule.executable_path;
            LVITEMW item{
                .mask = LVIF_TEXT, .iItem = static_cast<int>(index), .pszText = path.data()};
            ListView_InsertItem(list_, &item);
            const auto* profile = find_profile(profiles, rule.profile_id);
            const auto fixed = rule.policy == Policy::fixed;
            const char* status = !fixed || (profile && profile->selectable)
                ? "settings.input_method.available" : "settings.input_method.unavailable";
            if (fixed && profile && !profile->supports_chinese && rule.mode == Mode::chinese)
                status = "settings.input_method.unavailable";
            const std::array<std::wstring, 4> values{
                std::wstring(localized(policy_key(rule.policy))),
                !fixed ? L"" : profile ? profile->name
                    : std::wstring(localized("settings.input_method.unavailable_profile")),
                fixed ? std::wstring(localized(mode_key(rule.mode))) : L"",
                std::wstring(localized(status))};
            for (int column = 0; column < 4; ++column)
                ListView_SetItemText(list_, static_cast<int>(index), column + 1,
                    const_cast<wchar_t*>(values[column].c_str()));
        }
        if (selected >= 0 && selected < static_cast<int>(draft_.rules.size())) {
            ListView_SetItemState(list_, selected, LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
        }
        settings_visual_style::set_list_empty_text(list_, localized("settings.input_method.no_rules"));
        selection_changed();
    }

    void selection_changed() {
        const auto selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        const auto has_selection = selected >= 0 && selected < static_cast<int>(draft_.rules.size());
        EnableWindow(edit_, has_selection);
        EnableWindow(remove_, has_selection);
        text(path_, has_selection ? draft_.rules[static_cast<std::size_t>(selected)].executable_path : L"");
    }

    void update_status() {
        const auto state = services_.status();
        auto label = std::wstring(localized(state.key.c_str()));
        if (!state.path.empty()) label += L"  " + std::filesystem::path(state.path).filename().wstring();
        if (state.error) label += L" (" + std::to_wstring(state.error) + L")";
        text(status_, label);
    }

    void changed() { if (changed_) changed_(); }

    void command(int id) {
        if (id == enable_id) {
            draft_.enabled = SendMessageW(toggle_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            changed();
        } else if (id == refresh_id) {
            services_.refresh();
            refresh_rows();
        } else if (id == add_id || id == edit_id) {
            const auto selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
            if (id == edit_id && selected < 0) return;
            Rule initial;
            if (id == add_id) {
                const auto chosen = choose_application(instance_, GetAncestor(window_, GA_ROOT),
                    add_, *localization_, services_.applications);
                if (!chosen) return;
                initial.executable_path = *chosen;
            } else initial = draft_.rules[static_cast<std::size_t>(selected)];
            auto others = draft_.rules;
            if (id == edit_id) others.erase(others.begin() + selected);
            auto result = show_rule_dialog(instance_, GetAncestor(window_, GA_ROOT),
                *localization_, initial, services_.profiles(), others, services_.applications);
            if (!result || (id == edit_id && *result == initial)) return;
            if (id == add_id) draft_.rules.push_back(*result);
            else draft_.rules[static_cast<std::size_t>(selected)] = *result;
            refresh_rows();
            changed();
        } else if (id == remove_id) {
            const auto selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
            if (selected < 0) return;
            draft_.rules.erase(draft_.rules.begin() + selected);
            refresh_rows();
            changed();
        }
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* created = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created->lpCreateParams));
        }
        auto* self = reinterpret_cast<Page*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return DefWindowProcW(window, message, wparam, lparam);
        try {
            if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) {
                self->command(LOWORD(wparam));
                return 0;
            }
            if (message == WM_NOTIFY) {
                const auto* header = reinterpret_cast<NMHDR*>(lparam);
                if (header->idFrom == list_id) {
                    if (header->code == LVN_ITEMCHANGED) self->selection_changed();
                    else if (header->code == NM_DBLCLK) self->command(edit_id);
                    else if (header->code == LVN_GETINFOTIPW) {
                        auto* tip = reinterpret_cast<NMLVGETINFOTIPW*>(lparam);
                        if (tip->iItem >= 0 && tip->iItem < static_cast<int>(self->draft_.rules.size()))
                            wcsncpy_s(tip->pszText, tip->cchTextMax,
                                self->draft_.rules[static_cast<std::size_t>(tip->iItem)].executable_path.c_str(),
                                _TRUNCATE);
                    }
                }
            }
            if (message == WM_TIMER) { self->update_status(); return 0; }
            if (message == WM_ERASEBKGND)
                return settings_visual_style::erase_background(window, wparam);
            if (settings_visual_style::is_color_message(message))
                return settings_visual_style::handle_color_message(message, wparam, lparam);
        } catch (...) {
            text(self->status_, self->localized("settings.input_method.state_failed"));
        }
        if (message == WM_NCDESTROY) {
            KillTimer(window, 1);
            self->window_ = nullptr;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    Settings& draft_;
    PageServices services_;
    const Localization* localization_ = nullptr;
    std::function<void()> changed_;
    HINSTANCE instance_ = nullptr;
    HWND window_{}, heading_{}, label_{}, toggle_{}, add_{}, edit_{}, remove_{};
    HWND refresh_{}, list_{}, path_{}, status_{};
    UINT dpi_ = 96;
    settings_visual_style::PageTypography typography_;
};

} // namespace

std::unique_ptr<ISettingsPage> make_settings_page(Settings& draft, PageServices services) {
    return std::make_unique<Page>(draft, std::move(services));
}

std::optional<Rule> show_rule_dialog(
    HINSTANCE instance, HWND owner, const Localization& localization,
    Rule rule, const std::vector<Profile>& profiles, const std::vector<Rule>& other_rules,
    const ApplicationSelectionServices& applications) {
    return RuleDialog(instance, owner, localization, std::move(rule), profiles, other_rules,
        applications).run();
}

} // namespace simpilot::input_method
