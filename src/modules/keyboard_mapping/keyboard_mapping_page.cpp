#include "keyboard_mapping_module.hpp"
#include "keyboard_mapping_dialog.hpp"
#include "keyboard_manager.hpp"
#include "settings_visual_style.hpp"
#include "toggle_switch.hpp"
#include <commctrl.h>
#include <algorithm>
#include <format>
#include <stdexcept>

namespace simpilot {
namespace {

constexpr int keyboard_mapping_list_identifier = 600;
constexpr int keyboard_mapping_add_identifier = 601;
constexpr int keyboard_mapping_delete_identifier = 602;
constexpr int keyboard_mapping_edit_identifier = 603;
constexpr int keyboard_mapping_switch_identifier = 604;

std::optional<std::wstring> validation_message(
    const std::vector<KeyboardMappingRule>& rules, const Localization& localization) {
    if (rules.size() > maximum_keyboard_mappings) {
        return std::wstring(localization.text("settings.keyboard_mappings.limit_reached"));
    }
    const auto errors = validate_keyboard_mappings(rules);
    if (errors.empty()) return std::nullopt;
    const auto& error = errors.front();
    std::string_view key = "settings.keyboard_mappings.invalid";
    switch (error.kind) {
    case KeyboardMappingValidationKind::duplicate_source:
        key = "settings.keyboard_mappings.duplicate_source"; break;
    case KeyboardMappingValidationKind::modifier_conflict:
        key = "settings.keyboard_mappings.modifier_conflict"; break;
    case KeyboardMappingValidationKind::prefix_conflict:
        key = "settings.keyboard_mappings.prefix_conflict"; break;
    case KeyboardMappingValidationKind::cycle:
        key = "settings.keyboard_mappings.cycle"; break;
    default: return std::wstring(localization.text(key));
    }
    return std::wstring(localization.text(key)) + L" "
        + (error.process.empty() ? std::wstring(localization.text("settings.keyboard_mappings.global"))
                                 : error.process);
}

KeyboardMappingDialog::CaptureCallbacks mapping_capture_callbacks(
    KeyboardManager& keyboard_manager) {
    KeyboardMappingDialog::CaptureCallbacks callbacks;
    callbacks.begin_trigger = [&keyboard_manager](
        KeyboardMappingDialog::TriggerCaptureHandler handler) {
        return keyboard_manager.begin_mapping_capture(
            CaptureMode::mapping_trigger,
            [handler = std::move(handler)](
                const KeyboardMappingCaptureResult& result) mutable {
                if (result.kind == KeyboardMappingCaptureResultKind::captured) {
                    handler(result.trigger);
                } else {
                    handler(std::nullopt);
                }
            });
    };
    callbacks.begin_output = [&keyboard_manager](
        KeyboardMappingDialog::OutputCaptureHandler handler) {
        return keyboard_manager.begin_mapping_capture(
            CaptureMode::mapping_output,
            [handler = std::move(handler)](
                const KeyboardMappingCaptureResult& result) mutable {
                if (result.kind == KeyboardMappingCaptureResultKind::captured) {
                    handler(result.output);
                } else {
                    handler(std::nullopt);
                }
            });
    };
    callbacks.end = [&keyboard_manager] { keyboard_manager.end_mapping_capture(); };
    callbacks.foreground_process = [&keyboard_manager] {
        return keyboard_manager.last_external_foreground_process();
    };
    return callbacks;
}

class MappingSettingsPage final : public ISettingsPage {
public:
    MappingSettingsPage(KeyboardMappingSettings& settings, KeyboardManager& keyboard,
                        std::function<void(std::wstring_view)> diagnostic)
        : settings_(settings), keyboard_manager_(keyboard), diagnostic_(std::move(diagnostic)) {}
    ~MappingSettingsPage() override {
        keyboard_manager_.end_mapping_capture();
        if (window_) DestroyWindow(window_);
        if (images_) ImageList_Destroy(images_);
    }
    void create(const SettingsPageContext& context) override {
        instance_ = context.instance;
        changed_ = context.changed;
        localization_.set_language(std::string(context.localization.language_code()));
        language_code_ = localization_.language_code();
        WNDCLASSW type{
            .lpfnWndProc = procedure, .hInstance = instance_,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
            .lpszClassName = L"Simpilot.KeyboardMappingSettingsPage",
        };
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Cannot register keyboard mapping page");
        }
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, context.parent, nullptr, instance_, this);
        if (!window_) throw std::runtime_error("Cannot create keyboard mapping page");
        create_controls();
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        typography_.update(font, dpi);
        MoveWindow(window_, bounds.left, bounds.top, bounds.right - bounds.left,
                   bounds.bottom - bounds.top, TRUE);
        for (auto control : {keyboard_mapping_switch_, keyboard_mapping_list_, keyboard_mapping_add_button_,
                keyboard_mapping_edit_button_, keyboard_mapping_delete_button_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(typography_.body()), TRUE);
        }
        SendMessageW(keyboard_mapping_heading_, WM_SETFONT,
            reinterpret_cast<WPARAM>(typography_.title()), TRUE);
        if (dpi_ != dpi || !images_) {
            if (const auto next = toggle_switch::create_state_image_list(dpi)) {
                ListView_SetImageList(keyboard_mapping_list_, next, LVSIL_STATE);
                if (images_) ImageList_Destroy(images_);
                images_ = next;
            }
            dpi_ = dpi;
        }
        layout_controls(bounds.right - bounds.left, bounds.bottom - bounds.top, dpi);
    }
    void show(bool visible) override {
        if (!visible) keyboard_manager_.end_mapping_capture();
        ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
    }
    void refresh_language(const Localization& localization) override;
private:
    void create_controls();
    void layout_controls(int width, int height, UINT dpi);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            auto* page = static_cast<MappingSettingsPage*>(
                reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            page->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(page));
        }
        auto* page = reinterpret_cast<MappingSettingsPage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCDESTROY && page) {
            page->window_ = nullptr;
            return DefWindowProcW(window, message, wparam, lparam);
        }
        try {
            return page ? page->handle_message(message, wparam, lparam)
                        : DefWindowProcW(window, message, wparam, lparam);
        } catch (...) {
            if (page) page->diagnose(L"keyboard mapping page callback failed");
            return 0;
        }
    }
    void refresh_keyboard_mapping_list(std::optional<std::size_t> selection = std::nullopt);
    std::wstring keyboard_mapping_key_label(const PhysicalKey& key) const;
    std::wstring keyboard_mapping_trigger_label(const KeyboardTrigger& trigger) const;
    std::wstring keyboard_mapping_output_label(const KeyboardOutput& output) const;
    std::optional<std::size_t> selected_keyboard_mapping_index() const;
    void update_keyboard_mapping_buttons();
    void add_keyboard_mapping();
    void edit_selected_keyboard_mapping();
    void delete_selected_keyboard_mapping();
    bool commit_keyboard_mapping(KeyboardMappingRule candidate, std::optional<std::size_t> editing_index);
    const wchar_t* text(std::string_view key) const { return localization_.text(key).data(); }
    void mark_dirty() { if (changed_) changed_(); }
    void diagnose(std::wstring_view message) const noexcept {
        try { if (diagnostic_) diagnostic_(message); } catch (...) {}
    }
    KeyboardMappingSettings& settings_;
    KeyboardManager& keyboard_manager_;
    std::function<void(std::wstring_view)> diagnostic_;
    std::function<void()> changed_;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    Localization localization_{UiLanguage::simplified_chinese};
    std::string language_code_;
    HWND keyboard_mapping_heading_ = nullptr;
    HWND keyboard_mapping_switch_ = nullptr;
    HWND keyboard_mapping_list_ = nullptr;
    HWND keyboard_mapping_add_button_ = nullptr;
    HWND keyboard_mapping_edit_button_ = nullptr;
    HWND keyboard_mapping_delete_button_ = nullptr;
    settings_visual_style::PageTypography typography_;
    HIMAGELIST images_ = nullptr;
    UINT dpi_ = 0;
    bool refreshing_keyboard_mappings_ = false;
};
void MappingSettingsPage::create_controls() {
    keyboard_mapping_heading_ = CreateWindowW(
        L"STATIC", text("settings.keyboard_mappings.heading"), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    keyboard_mapping_switch_ = toggle_switch::create(
        instance_, window_, keyboard_mapping_switch_identifier,
        text("settings.keyboard_mappings.enabled"),
        settings_.enabled, true);
    keyboard_mapping_list_ = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL
            | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(keyboard_mapping_list_identifier)), instance_, nullptr);
    ListView_SetExtendedListViewStyle(keyboard_mapping_list_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES | LVS_EX_LABELTIP);
    settings_visual_style::style_list_view(keyboard_mapping_list_);
    LVCOLUMNW mapping_column{.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM,
                             .cx = 72,
                             .pszText = const_cast<wchar_t*>(
                                 text("settings.keyboard_mappings.column.enabled"))};
    ListView_InsertColumn(keyboard_mapping_list_, 0, &mapping_column);
    mapping_column.iSubItem = 1;
    mapping_column.cx = 190;
    mapping_column.pszText = const_cast<wchar_t*>(
        text("settings.keyboard_mappings.column.purpose"));
    ListView_InsertColumn(keyboard_mapping_list_, 1, &mapping_column);
    mapping_column.iSubItem = 2;
    mapping_column.cx = 250;
    mapping_column.pszText = const_cast<wchar_t*>(
        text("settings.keyboard_mappings.column.source"));
    ListView_InsertColumn(keyboard_mapping_list_, 2, &mapping_column);
    mapping_column.iSubItem = 3;
    mapping_column.cx = 220;
    mapping_column.pszText = const_cast<wchar_t*>(
        text("settings.keyboard_mappings.column.target"));
    ListView_InsertColumn(keyboard_mapping_list_, 3, &mapping_column);
    mapping_column.iSubItem = 4;
    mapping_column.cx = 180;
    mapping_column.pszText = const_cast<wchar_t*>(
        text("settings.keyboard_mappings.column.process"));
    ListView_InsertColumn(keyboard_mapping_list_, 4, &mapping_column);
    keyboard_mapping_add_button_ = CreateWindowW(
        L"BUTTON", text("settings.add"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(keyboard_mapping_add_identifier)), instance_, nullptr);
    keyboard_mapping_edit_button_ = CreateWindowW(
        L"BUTTON", text("settings.edit"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(keyboard_mapping_edit_identifier)), instance_, nullptr);
    keyboard_mapping_delete_button_ = CreateWindowW(
        L"BUTTON", text("settings.delete"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(keyboard_mapping_delete_identifier)), instance_, nullptr);
    for (auto button : {keyboard_mapping_add_button_, keyboard_mapping_edit_button_,
                        keyboard_mapping_delete_button_}) {
        settings_visual_style::style_button(button);
    }
    refresh_keyboard_mapping_list();

}
void MappingSettingsPage::refresh_language(const Localization& localization) {
    localization_.set_language(std::string(localization.language_code()));
    language_code_ = localization_.language_code();
    settings_visual_style::set_list_empty_text(keyboard_mapping_list_, text("settings.keyboard_mappings.empty"));
    LVCOLUMNW column{.mask = LVCF_TEXT};
    SetWindowTextW(keyboard_mapping_heading_,
                   text("settings.keyboard_mappings.heading"));
    SetWindowTextW(keyboard_mapping_switch_,
                   text("settings.keyboard_mappings.enabled"));
    const std::array mapping_columns{
        text("settings.keyboard_mappings.column.enabled"),
        text("settings.keyboard_mappings.column.purpose"),
        text("settings.keyboard_mappings.column.source"),
        text("settings.keyboard_mappings.column.target"),
        text("settings.keyboard_mappings.column.process"),
    };
    for (int index = 0; index < static_cast<int>(mapping_columns.size()); ++index) {
        column.pszText = const_cast<wchar_t*>(mapping_columns[static_cast<std::size_t>(index)]);
        ListView_SetColumn(keyboard_mapping_list_, index, &column);
    }
    SetWindowTextW(keyboard_mapping_add_button_, text("settings.add"));
    SetWindowTextW(keyboard_mapping_edit_button_, text("settings.edit"));
    SetWindowTextW(keyboard_mapping_delete_button_, text("settings.delete"));
    const auto mapping_selection = selected_keyboard_mapping_index();
    refresh_keyboard_mapping_list(mapping_selection);

}
void MappingSettingsPage::layout_controls(int width, int height, UINT dpi) {
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    MoveWindow(keyboard_mapping_heading_, 0, 0, width, scale(36), TRUE);
    settings_visual_style::style_button(keyboard_mapping_add_button_, settings_visual_style::ButtonStyle::add);
    settings_visual_style::style_button(keyboard_mapping_edit_button_, settings_visual_style::ButtonStyle::edit);
    settings_visual_style::style_button(keyboard_mapping_delete_button_, settings_visual_style::ButtonStyle::remove);
    const int gap = scale(8), button_width = scale(32);
    const int toolbar_width = 3 * button_width + 2 * gap;
    const bool stacked = width < scale(220) + toolbar_width + scale(24);
    MoveWindow(keyboard_mapping_switch_, 0, scale(56),
               std::min(width, scale(220)), scale(32), TRUE);
    const int toolbar_y = scale(stacked ? 100 : 56);
    const int toolbar_x = stacked ? 0 : width - toolbar_width;
    MoveWindow(keyboard_mapping_add_button_, toolbar_x, toolbar_y,
               button_width, scale(32), TRUE);
    MoveWindow(keyboard_mapping_edit_button_, toolbar_x + button_width + gap, toolbar_y,
               button_width, scale(32), TRUE);
    MoveWindow(keyboard_mapping_delete_button_, toolbar_x + 2 * (button_width + gap), toolbar_y,
               button_width, scale(32), TRUE);
    const int list_y = toolbar_y + scale(44);
    MoveWindow(keyboard_mapping_list_, 0, list_y, width, std::max(1, height - list_y), TRUE);
    RECT list_client{};
    GetClientRect(keyboard_mapping_list_, &list_client);
    const int available = std::max(1L, list_client.right - scale(4));
    const int enabled = scale(64);
    const int source = scale(148), target = scale(148);
    const int remaining = std::max(scale(224), available - enabled - source - target);
    const int purpose = std::max(scale(112), remaining * 55 / 100);
    ListView_SetColumnWidth(keyboard_mapping_list_, 0, enabled);
    ListView_SetColumnWidth(keyboard_mapping_list_, 1, purpose);
    ListView_SetColumnWidth(keyboard_mapping_list_, 2, source);
    ListView_SetColumnWidth(keyboard_mapping_list_, 3, target);
    ListView_SetColumnWidth(keyboard_mapping_list_, 4,
        std::max(scale(112), remaining - purpose));
}
std::wstring MappingSettingsPage::keyboard_mapping_key_label(
    const PhysicalKey& key) const {
    return localized_keyboard_mapping_key_label(key, localization_);
}

std::wstring MappingSettingsPage::keyboard_mapping_trigger_label(
    const KeyboardTrigger& trigger) const {
    if (trigger.single_key) return keyboard_mapping_key_label(trigger.action);
    std::wstring result;
    const auto append = [this, &result](const PhysicalKey& key) {
        if (!result.empty()) result.append(L" + ");
        result.append(keyboard_mapping_key_label(key));
    };
    const auto count = std::min(trigger.modifier_count,
                                trigger.modifiers.size());
    for (std::size_t index = 0; index < count; ++index) {
        append(trigger.modifiers[index]);
    }
    append(trigger.action);
    if (trigger.chord_action) append(*trigger.chord_action);
    return result;
}

std::wstring MappingSettingsPage::keyboard_mapping_output_label(
    const KeyboardOutput& output) const {
    if (output.single_key) return keyboard_mapping_key_label(output.action);
    std::wstring result;
    const auto append = [this, &result](const PhysicalKey& key) {
        if (!result.empty()) result.append(L" + ");
        result.append(keyboard_mapping_key_label(key));
    };
    const auto count = std::min(output.modifier_count,
                                output.modifiers.size());
    for (std::size_t index = 0; index < count; ++index) {
        append(output.modifiers[index]);
    }
    append(output.action);
    return result;
}

void MappingSettingsPage::refresh_keyboard_mapping_list(
    const std::optional<std::size_t> selection) {
    if (!keyboard_mapping_list_) return;
    refreshing_keyboard_mappings_ = true;
    ListView_DeleteAllItems(keyboard_mapping_list_);
    for (std::size_t index = 0; index < settings_.rules.size(); ++index) {
        const auto& mapping = settings_.rules[index];
        wchar_t empty[] = L"";
        LVITEMW item{
            .mask = LVIF_TEXT | LVIF_PARAM,
            .iItem = static_cast<int>(index),
            .pszText = empty,
            .lParam = static_cast<LPARAM>(index),
        };
        const auto row = ListView_InsertItem(keyboard_mapping_list_, &item);
        ListView_SetCheckState(keyboard_mapping_list_, row,
                               mapping.enabled ? TRUE : FALSE);
        const auto source = keyboard_mapping_trigger_label(mapping.trigger);
        const auto target = keyboard_mapping_output_label(mapping.output);
        const auto purpose = mapping.purpose;
        std::wstring process;
        for (const auto& name : mapping.process_names) {
            if (!process.empty()) process.append(L"; ");
            process.append(name);
        }
        if (process.empty()) process = text("settings.keyboard_mappings.global");
        else if (mapping.application_scope == ApplicationScope::excluded)
            process = std::wstring(text("settings.keyboard_mappings.excluded")) + L": " + process;
        ListView_SetItemText(keyboard_mapping_list_, row, 1,
                             const_cast<wchar_t*>(purpose.c_str()));
        ListView_SetItemText(keyboard_mapping_list_, row, 2,
                             const_cast<wchar_t*>(source.c_str()));
        ListView_SetItemText(keyboard_mapping_list_, row, 3,
                             const_cast<wchar_t*>(target.c_str()));
        ListView_SetItemText(keyboard_mapping_list_, row, 4,
                             const_cast<wchar_t*>(process.c_str()));
        if (selection && *selection == index) {
            ListView_SetItemState(keyboard_mapping_list_, row,
                                  LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(keyboard_mapping_list_, row, FALSE);
        }
    }
    refreshing_keyboard_mappings_ = false;
    update_keyboard_mapping_buttons();
}

std::optional<std::size_t> MappingSettingsPage::selected_keyboard_mapping_index() const {
    const auto row = ListView_GetNextItem(keyboard_mapping_list_, -1, LVNI_SELECTED);
    if (row < 0) return std::nullopt;
    LVITEMW item{.mask = LVIF_PARAM, .iItem = row};
    if (!ListView_GetItem(keyboard_mapping_list_, &item)) return std::nullopt;
    const auto index = static_cast<std::size_t>(item.lParam);
    return index < settings_.rules.size()
        ? std::optional<std::size_t>(index) : std::nullopt;
}

void MappingSettingsPage::update_keyboard_mapping_buttons() {
    const auto selected = selected_keyboard_mapping_index().has_value();
    EnableWindow(keyboard_mapping_edit_button_, selected ? TRUE : FALSE);
    EnableWindow(keyboard_mapping_delete_button_, selected ? TRUE : FALSE);
}

void MappingSettingsPage::add_keyboard_mapping() {
    auto callbacks = mapping_capture_callbacks(keyboard_manager_);
    callbacks.validate = [this](const KeyboardMappingRule& candidate) {
        auto next = settings_.rules;
        next.push_back(candidate);
        return validation_message(next, localization_);
    };
    auto candidate = KeyboardMappingDialog::show_modal(
        instance_, window_, language_code_, nullptr,
        std::move(callbacks),
        [this](const std::wstring_view message) { diagnose(message); });
    if (candidate && commit_keyboard_mapping(std::move(*candidate), std::nullopt)) {
        mark_dirty();
    }
}

void MappingSettingsPage::edit_selected_keyboard_mapping() {
    const auto index = selected_keyboard_mapping_index();
    if (!index) return;
    auto callbacks = mapping_capture_callbacks(keyboard_manager_);
    callbacks.validate = [this, index](const KeyboardMappingRule& candidate) {
        auto next = settings_.rules;
        next[*index] = candidate;
        return validation_message(next, localization_);
    };
    auto candidate = KeyboardMappingDialog::show_modal(
        instance_, window_, language_code_, &settings_.rules[*index],
        std::move(callbacks),
        [this](const std::wstring_view message) { diagnose(message); });
    if (candidate && commit_keyboard_mapping(std::move(*candidate), index)) {
        mark_dirty();
    }
}

void MappingSettingsPage::delete_selected_keyboard_mapping() {
    const auto index = selected_keyboard_mapping_index();
    if (!index) return;
    if (MessageBoxW(window_, text("settings.keyboard_mappings.delete_confirm"),
                    text("settings.title"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    settings_.rules.erase(settings_.rules.begin()
        + static_cast<std::ptrdiff_t>(*index));
    refresh_keyboard_mapping_list();
    mark_dirty();
}

bool MappingSettingsPage::commit_keyboard_mapping(
    KeyboardMappingRule candidate, const std::optional<std::size_t> editing_index) {
    auto next = settings_.rules;
    if (editing_index) {
        if (*editing_index >= next.size()) return false;
        next[*editing_index] = std::move(candidate);
    } else {
        if (next.size() >= 128) {
            MessageBoxW(window_, text("settings.keyboard_mappings.limit_reached"),
                        text("settings.title"), MB_OK | MB_ICONWARNING);
            return false;
        }
        next.push_back(std::move(candidate));
    }
    const auto errors = validate_keyboard_mappings(next);
    if (!errors.empty()) {
        diagnose(std::format(L"keyboard mapping validation failed rule={} reason={}",
                             errors.front().rule_index, errors.front().message));
        MessageBoxW(window_, text("settings.keyboard_mappings.invalid"),
                    text("settings.title"), MB_OK | MB_ICONWARNING);
        return false;
    }
    settings_.rules = std::move(next);
    refresh_keyboard_mapping_list(editing_index);
    return true;
}

LRESULT MappingSettingsPage::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_ERASEBKGND) return settings_visual_style::erase_background(window_, wparam);
    if (settings_visual_style::is_color_message(message)) {
        return settings_visual_style::handle_color_message(message, wparam, lparam);
    }
    if (message == WM_NOTIFY) {
        const auto* notification = reinterpret_cast<NMHDR*>(lparam);
        if (notification && notification->hwndFrom == keyboard_mapping_list_) {
            if (notification->code == LVN_ITEMCHANGED) {
                update_keyboard_mapping_buttons();
                if (refreshing_keyboard_mappings_) return 0;
                const auto* changed = reinterpret_cast<const NMLISTVIEW*>(lparam);
                if ((changed->uChanged & LVIF_STATE) != 0
                    && ((changed->uOldState ^ changed->uNewState)
                        & LVIS_STATEIMAGEMASK) != 0
                    && changed->iItem >= 0) {
                    LVITEMW item{.mask = LVIF_PARAM, .iItem = changed->iItem};
                    if (ListView_GetItem(keyboard_mapping_list_, &item)) {
                        const auto index = static_cast<std::size_t>(item.lParam);
                        if (index < settings_.rules.size()) {
                            auto next = settings_.rules;
                            next[index].enabled = ListView_GetCheckState(
                                keyboard_mapping_list_, changed->iItem) != FALSE;
                            const auto errors = validate_keyboard_mappings(next);
                            if (!errors.empty()) {
                                refreshing_keyboard_mappings_ = true;
                                ListView_SetCheckState(keyboard_mapping_list_,
                                    changed->iItem,
                                    settings_.rules[index].enabled ? TRUE : FALSE);
                                refreshing_keyboard_mappings_ = false;
                                MessageBoxW(window_, text("settings.keyboard_mappings.invalid"),
                                    text("settings.title"), MB_OK | MB_ICONWARNING);
                            } else {
                                settings_.rules = std::move(next);
                                mark_dirty();
                            }
                        }
                    }
                }
                return 0;
            }
            if (notification->code == NM_DBLCLK) {
                const auto* activated = reinterpret_cast<const NMITEMACTIVATE*>(lparam);
                if (activated->iItem >= 0) edit_selected_keyboard_mapping();
                return 0;
            }
        }
    }
    if (message == WM_COMMAND) {
        const auto identifier = LOWORD(wparam);
        if (identifier == keyboard_mapping_switch_identifier
            && HIWORD(wparam) == BN_CLICKED) {
            settings_.enabled = SendMessageW(
                keyboard_mapping_switch_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            mark_dirty();
            return 0;
        }
        if (identifier == keyboard_mapping_add_identifier
            && HIWORD(wparam) == BN_CLICKED) {
            add_keyboard_mapping();
            return 0;
        }
        if (identifier == keyboard_mapping_edit_identifier
            && HIWORD(wparam) == BN_CLICKED) {
            edit_selected_keyboard_mapping();
            return 0;
        }
        if (identifier == keyboard_mapping_delete_identifier
            && HIWORD(wparam) == BN_CLICKED) {
            delete_selected_keyboard_mapping();
            return 0;
        }
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}
} // namespace
std::unique_ptr<ISettingsPage> make_keyboard_mapping_page(
    KeyboardMappingSettings& settings, KeyboardManager& keyboard,
    std::function<void(std::wstring_view)> diagnose) {
    return std::make_unique<MappingSettingsPage>(settings, keyboard, std::move(diagnose));
}
} // namespace simpilot
