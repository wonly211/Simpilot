#include "custom_hotkey_settings.hpp"
#include "custom_hotkey_dialog.hpp"
#include "hotkey_settings_page.hpp"
#include "settings_visual_style.hpp"
#include "toggle_switch.hpp"
#include <commctrl.h>
#include <array>

namespace simpilot {
namespace {
class CustomHotkeySection final : public ISettingsPage {
public:
    CustomHotkeySection(CustomHotkeySettings& draft, HotkeyRegistry& hotkeys,
        KeyboardManager& keyboard, std::filesystem::path directory,
        std::function<void(std::wstring_view)> diagnose)
        : draft_(draft), hotkeys_(hotkeys), keyboard_(keyboard),
          directory_(std::move(directory)), diagnose_(std::move(diagnose)) {}
    ~CustomHotkeySection() override {
        observer_.reset();
        if (window_) DestroyWindow(window_);
        if (images_) ImageList_Destroy(images_);
    }
    void create(const SettingsPageContext& context) override {
        instance_ = context.instance;
        localization_ = &context.localization;
        WNDCLASSW wc{};
        wc.lpfnWndProc = procedure;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"Simpilot.CustomHotkeySection";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Cannot register custom hotkey section");
        }
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, context.parent, nullptr, instance_, this);
        if (!window_) throw std::runtime_error("Cannot create custom hotkey section");
        heading_ = control(L"STATIC", 0, 0);
        add_ = control(L"BUTTON", 1, WS_TABSTOP | BS_PUSHBUTTON);
        edit_ = control(L"BUTTON", 2, WS_TABSTOP | BS_PUSHBUTTON);
        remove_ = control(L"BUTTON", 3, WS_TABSTOP | BS_PUSHBUTTON);
        list_ = control(WC_LISTVIEWW, 4, WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL
            | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
        ListView_SetExtendedListViewStyle(list_,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES | LVS_EX_LABELTIP);
        settings_visual_style::style_list_view(list_);
        for (int i = 0; i < 4; ++i) {
            LVCOLUMNW column{.mask = LVCF_WIDTH | LVCF_SUBITEM, .cx = 100, .iSubItem = i};
            ListView_InsertColumn(list_, i, &column);
        }
        observer_ = hotkeys_.observe_drafts("custom_hotkey.section", [this] { refresh(); });
        refresh_language(context.localization);
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        MoveWindow(window_, bounds.left, bounds.top, std::max(1L, bounds.right - bounds.left),
                   std::max(1L, bounds.bottom - bounds.top), TRUE);
        const int width = std::max(1L, bounds.right - bounds.left);
        const int height = std::max(1L, bounds.bottom - bounds.top);
        MoveWindow(heading_, 0, 0, width, scale(28), TRUE);
        const int button_width = std::min(scale(100), std::max(1, width / 3 - scale(8)));
        MoveWindow(add_, 0, scale(34), button_width, scale(34), TRUE);
        MoveWindow(edit_, button_width + scale(8), scale(34), button_width, scale(34), TRUE);
        MoveWindow(remove_, 2 * (button_width + scale(8)), scale(34), button_width, scale(34), TRUE);
        MoveWindow(list_, 0, scale(76), width, std::max(1, height - scale(76)), TRUE);
        const int widths[]{scale(72), scale(164), scale(130)};
        for (int i = 0; i < 3; ++i) ListView_SetColumnWidth(list_, i, widths[i]);
        ListView_SetColumnWidth(list_, 3, std::max(scale(160), width - widths[0] - widths[1] - widths[2]));
        for (auto control : {heading_, add_, edit_, remove_, list_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        if (dpi_ != dpi || !images_) {
            const auto next = toggle_switch::create_state_image_list(dpi);
            if (next) {
                ListView_SetImageList(list_, next, LVSIL_STATE);
                if (images_) ImageList_Destroy(images_);
                images_ = next;
            }
            dpi_ = dpi;
        }
    }
    void show(bool visible) override { ShowWindow(window_, visible ? SW_SHOW : SW_HIDE); }
    void refresh_language(const Localization& localization) override {
        localization_ = &localization;
        SetWindowTextW(heading_, text("settings.section.custom_hotkeys"));
        SetWindowTextW(add_, text("settings.add"));
        SetWindowTextW(edit_, text("settings.edit"));
        SetWindowTextW(remove_, text("settings.delete"));
        constexpr std::string_view columns[]{"settings.global_hotkeys.column.enabled",
            "settings.global_hotkeys.column.hotkey", "settings.global_hotkeys.column.action",
            "settings.global_hotkeys.column.target"};
        for (int i = 0; i < 4; ++i) {
            LVCOLUMNW column{.mask = LVCF_TEXT, .pszText = const_cast<wchar_t*>(text(columns[i]))};
            ListView_SetColumn(list_, i, &column);
        }
        refresh();
    }
private:
    const wchar_t* text(std::string_view key) const { return localization_->text(key).data(); }
    HWND control(const wchar_t* type, int id, DWORD style) {
        const auto value = CreateWindowW(type, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
            window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (!value) throw std::runtime_error("Cannot create custom hotkey control");
        return value;
    }
    std::optional<std::size_t> selected() const {
        const auto index = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        return index >= 0 && static_cast<std::size_t>(index) < draft_.items.size()
            ? std::optional<std::size_t>(index) : std::nullopt;
    }
    void buttons() {
        EnableWindow(add_, draft_.items.size() < 128);
        EnableWindow(edit_, selected().has_value());
        EnableWindow(remove_, selected().has_value());
    }
    void refresh(std::optional<std::size_t> selection = std::nullopt) {
        if (!list_) return;
        if (!selection) selection = selected();
        refreshing_ = true;
        ListView_DeleteAllItems(list_);
        for (std::size_t i = 0; i < draft_.items.size(); ++i) {
            const auto& item = draft_.items[i];
            wchar_t empty[] = L"";
            LVITEMW row{.mask = LVIF_TEXT, .iItem = static_cast<int>(i), .pszText = empty};
            const auto index = ListView_InsertItem(list_, &row);
            ListView_SetCheckState(list_, index, item.enabled);
            auto gesture = item.binding.gesture ? item.binding.gesture->display_text() : std::wstring{};
            ListView_SetItemText(list_, index, 1, gesture.data());
            ListView_SetItemText(list_, index, 2, const_cast<wchar_t*>(text(
                item.action == CustomHotKeyAction::open_folder ? "custom_hotkey.open_folder"
                : item.action == CustomHotKeyAction::open_file ? "custom_hotkey.open_file"
                : "custom_hotkey.open_application")));
            ListView_SetItemText(list_, index, 3, const_cast<wchar_t*>(item.program_path.c_str()));
            if (selection == i) ListView_SetItemState(list_, index,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        refreshing_ = false;
        buttons();
    }
    void edit(bool add) {
        const auto index = add ? std::optional<std::size_t>{} : selected();
        if ((!add && !index) || (add && draft_.items.size() >= 128)) return;
        auto candidate = CustomHotKeyDialog::show_modal(instance_, window_,
            std::string(localization_->language_code()), keyboard_, directory_,
            index ? &draft_.items[*index] : nullptr, diagnose_);
        if (!candidate || !candidate->binding.gesture) return;
        auto binding = BuiltInHotKey{candidate->binding, candidate->enabled};
        if ((!index || draft_.items[*index].binding.gesture != binding.binding.gesture)
            && !confirm_hotkey_availability(window_, *localization_, binding,
                [this](const auto& gesture) { return keyboard_.probe_available(gesture); })) return;
        candidate->binding = binding.binding;
        // Allocate before any other owner's draft is cleared.
        if (add) draft_.items.reserve(draft_.items.size() + 1);
        const auto target = index.value_or(draft_.items.size());
        const auto id = "custom_hotkey." + std::to_string(target);
        const auto applied = hotkeys_.replace_conflicts(id, binding, [&](const auto& conflict) {
            return confirm_hotkey_replacement(window_, *localization_, *binding.binding.gesture, conflict);
        }, [&] {
            if (index) draft_.items[*index] = std::move(*candidate);
            else draft_.items.push_back(std::move(*candidate));
        });
        if (applied) refresh(target);
    }
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) {
            if (LOWORD(wparam) == 1) edit(true);
            if (LOWORD(wparam) == 2) edit(false);
            if (LOWORD(wparam) == 3) {
                const auto index = selected();
                if (index && MessageBoxW(window_, text("settings.custom_hotkeys.delete_confirm"),
                    text("settings.title"), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                    draft_.items.erase(draft_.items.begin() + static_cast<std::ptrdiff_t>(*index));
                    hotkeys_.drafts_changed();
                }
            }
            return 0;
        }
        if (message == WM_NOTIFY) {
            const auto* notification = reinterpret_cast<NMHDR*>(lparam);
            if (notification && notification->hwndFrom == list_) {
                if (notification->code == NM_DBLCLK
                    && reinterpret_cast<NMITEMACTIVATE*>(lparam)->iSubItem != 0) edit(false);
                if (notification->code == LVN_ITEMCHANGED && !refreshing_) {
                    buttons();
                    const auto* change = reinterpret_cast<NMLISTVIEW*>(lparam);
                    if ((change->uChanged & LVIF_STATE) && change->iItem >= 0
                        && ((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK)
                        && static_cast<std::size_t>(change->iItem) < draft_.items.size()) {
                        auto& item = draft_.items[change->iItem];
                        item.enabled = item.binding.gesture
                            && ListView_GetCheckState(list_, change->iItem) != FALSE;
                        hotkeys_.drafts_changed();
                    }
                }
            }
            return 0;
        }
        if (message == WM_ERASEBKGND) return settings_visual_style::erase_background(window_, wparam);
        if (settings_visual_style::is_color_message(message)) {
            return settings_visual_style::handle_color_message(message, wparam, lparam);
        }
        return DefWindowProcW(window_, message, wparam, lparam);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            auto* page = static_cast<CustomHotkeySection*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            page->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(page));
        }
        auto* page = reinterpret_cast<CustomHotkeySection*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!page) return DefWindowProcW(window, message, wparam, lparam);
        try {
            const auto result = page->message(message, wparam, lparam);
            if (message == WM_NCDESTROY) page->window_ = nullptr;
            return result;
        } catch (...) { return message == WM_CREATE ? -1 : 0; }
    }
    CustomHotkeySettings& draft_;
    HotkeyRegistry& hotkeys_;
    KeyboardManager& keyboard_;
    std::filesystem::path directory_;
    std::function<void(std::wstring_view)> diagnose_;
    Registration observer_;
    const Localization* localization_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr, heading_ = nullptr, add_ = nullptr, edit_ = nullptr,
         remove_ = nullptr, list_ = nullptr;
    HIMAGELIST images_ = nullptr;
    UINT dpi_ = 0;
    bool refreshing_ = false;
};
}
std::unique_ptr<ISettingsPage> make_custom_hotkey_section(
    CustomHotkeySettings& draft, HotkeyRegistry& hotkeys, KeyboardManager& keyboard,
    const std::filesystem::path& directory, std::function<void(std::wstring_view)> diagnose) {
    return std::make_unique<CustomHotkeySection>(draft, hotkeys, keyboard, directory, std::move(diagnose));
}
} // namespace simpilot
