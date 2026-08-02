#include "menu_editor_window.hpp"

#include "settings_visual_style.hpp"

#include "simpilot/command.hpp"
#include "simpilot/config_file.hpp"
#include "simpilot/menu_parser.hpp"
#include "simpilot/menu_writer.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj_core.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <cwchar>
#include <stdexcept>
#include <utility>

namespace simpilot {
namespace {

constexpr auto editor_class_name = L"Simpilot.MenuEditorWindow";
constexpr auto product_name = L"\u7b80\u9a6d | Simpilot";
constexpr int main_menu_identifier = 100;
constexpr int tree_identifier = 101;
constexpr int second_menu_identifier = 102;
constexpr int add_item_identifier = 200;
constexpr int add_category_identifier = 201;
constexpr int add_separator_identifier = 202;
constexpr int delete_identifier = 204;
constexpr int move_up_identifier = 205;
constexpr int move_down_identifier = 206;
constexpr int decrease_level_identifier = 209;
constexpr int increase_level_identifier = 210;
constexpr int name_identifier = 300;
constexpr int access_key_identifier = 301;
constexpr int type_identifier = 302;
constexpr int target_identifier = 303;
constexpr int browse_identifier = 304;
constexpr int arguments_identifier = 305;
constexpr int administrator_identifier = 306;
constexpr int splitter_identifier = 307;

const wchar_t* tr(const UiLanguage language, const wchar_t* chinese,
                  const wchar_t* english) noexcept {
    return language == UiLanguage::simplified_chinese ? chinese : english;
}

void set_font(const HWND control, const HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring control_text(const HWND control) {
    const auto length = GetWindowTextLengthW(control);
    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(length));
    return result;
}

std::wstring trim(std::wstring value) {
    const auto first = std::find_if_not(value.begin(), value.end(), iswspace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), iswspace).base();
    return first < last ? std::wstring(first, last) : std::wstring{};
}

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

bool application_extension(const std::filesystem::path& path) {
    const auto extension = lowercase(path.extension().wstring());
    static constexpr std::array extensions{
        L".exe", L".lnk", L".bat", L".cmd", L".vbs", L".ps1", L".ahk"};
    return std::ranges::find(extensions, extension) != extensions.end();
}

int item_type(const MenuEntry& entry) {
    if (entry.kind == MenuEntryKind::web) return 3;
    std::error_code path_error;
    if (std::filesystem::is_directory(entry.value, path_error)) return 1;
    path_error.clear();
    if (std::filesystem::is_regular_file(entry.value, path_error)
        && !application_extension(entry.value)) return 2;
    const auto parsed = ParsedCommand::try_parse(entry.value);
    if (!parsed) return 2;
    if (!parsed->arguments.empty() || application_extension(parsed->executable)) return 0;
    std::error_code error;
    return std::filesystem::is_directory(parsed->executable, error) ? 1 : 2;
}

bool web_url(const std::wstring& value) {
    const auto normalized = lowercase(value);
    return normalized.starts_with(L"http://") || normalized.starts_with(L"https://");
}

std::optional<MenuEditorWindow::ElementLocation> find_location_in(
    MenuCategory& category, MenuElement* target) {
    for (std::size_t index = 0; index < category.children.size(); ++index) {
        auto* element = category.children[index].get();
        if (element == target) return MenuEditorWindow::ElementLocation{&category, index, element};
        if (auto* nested = dynamic_cast<MenuCategory*>(element)) {
            if (auto found = find_location_in(*nested, target)) return found;
        }
    }
    return std::nullopt;
}

} // namespace

MenuEditorWindow::MenuEditorWindow(
    const HINSTANCE instance, const HWND parent, const UiLanguage language,
    std::filesystem::path main_menu_path, std::filesystem::path second_menu_path,
    DiagnosticSink diagnostic_sink, DirtySink dirty_sink)
    : instance_(instance), parent_(parent), language_(language),
      paths_{std::move(main_menu_path), std::move(second_menu_path)},
      diagnostic_sink_(std::move(diagnostic_sink)), dirty_sink_(std::move(dirty_sink)) {}

MenuEditorWindow::~MenuEditorWindow() {
    if (window_) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
    if (section_font_) DeleteObject(section_font_);
}

bool MenuEditorWindow::create() {
    try {
        load_documents();
    } catch (const std::exception&) {
        diagnose(L"menu editor load failed");
        MessageBoxW(parent_,
            tr(language_, L"\u65e0\u6cd5\u8bfb\u53d6\u5feb\u6377\u542f\u52a8\u83dc\u5355\u3002\u8bf7\u68c0\u67e5 UTF-8 \u7f16\u7801\u548c\u914d\u7f6e\u683c\u5f0f\u3002",
               L"The quick-launch menus could not be read. Check their UTF-8 encoding and configuration format."),
            product_name, MB_OK | MB_ICONERROR);
        return false;
    }
    INITCOMMONCONTROLSEX common_controls{
        .dwSize = sizeof(common_controls),
        .dwICC = ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES,
    };
    InitCommonControlsEx(&common_controls);
    const WNDCLASSW window_class{
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &MenuEditorWindow::window_procedure,
        .hInstance = instance_,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = editor_class_name,
    };
    RegisterClassW(&window_class);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, editor_class_name, L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, parent_, nullptr, instance_, this);
    if (!window_) return false;
    return true;
}

void MenuEditorWindow::set_bounds(
    const int x, const int y, const int width, const int height) const {
    if (window_) MoveWindow(window_, x, y, width, height, TRUE);
}

void MenuEditorWindow::set_visible(const bool visible) const {
    if (window_) ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
}

bool MenuEditorWindow::dirty() const noexcept {
    return dirty_;
}

void MenuEditorWindow::load_documents() {
    for (std::size_t index = 0; index < paths_.size(); ++index) {
        original_sources_[index] = read_source(paths_[index]);
        documents_[index] = std::make_unique<MenuDocument>(MenuParser::parse(
            original_sources_[index].value_or(L"")));
    }
}

void MenuEditorWindow::create_controls() {
    dpi_ = GetDpiForWindow(window_);
    main_menu_button_ = CreateWindowW(L"BUTTON",
        tr(language_, L"\u4e3b\u83dc\u5355", L"Main menu"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(main_menu_identifier)), instance_, nullptr);
    second_menu_button_ = CreateWindowW(L"BUTTON",
        tr(language_, L"\u7b2c\u4e8c\u83dc\u5355", L"Second menu"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(second_menu_identifier)), instance_, nullptr);
    SendMessageW(main_menu_button_, BM_SETCHECK, BST_CHECKED, 0);
    tree_ = CreateWindowExW(WS_EX_STATICEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_FULLROWSELECT
            | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(tree_identifier)), instance_, nullptr);
    settings_visual_style::style_tree_view(tree_);
    SetWindowSubclass(tree_, &MenuEditorWindow::tree_procedure, 1,
                      reinterpret_cast<DWORD_PTR>(this));
    splitter_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOTIFY,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(splitter_identifier)), instance_, nullptr);
    SetWindowSubclass(splitter_, &MenuEditorWindow::splitter_procedure, 1,
                      reinterpret_cast<DWORD_PTR>(this));
    detail_title_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    const auto create_label = [&](const wchar_t* text) {
        return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    };
    name_label_ = create_label(tr(language_, L"\u540d\u79f0", L"Name"));
    name_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(name_identifier)), instance_, nullptr);
    access_key_label_ = create_label(tr(language_, L"\u83dc\u5355\u5feb\u6377\u952e\uff08\u53ef\u9009\uff09", L"Menu access key (optional)"));
    access_key_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(access_key_identifier)), instance_, nullptr);
    SendMessageW(access_key_edit_, EM_SETLIMITTEXT, 1, 0);
    type_label_ = create_label(tr(language_, L"\u64cd\u4f5c\u7c7b\u578b", L"Action type"));
    type_combo_ = CreateWindowW(WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(type_identifier)), instance_, nullptr);
    const std::array<const wchar_t*, 4> types{
        tr(language_, L"\u6253\u5f00\u5e94\u7528", L"Open application"),
        tr(language_, L"\u6253\u5f00\u6587\u4ef6\u5939", L"Open folder"),
        tr(language_, L"\u6253\u5f00\u6587\u4ef6", L"Open file"),
        tr(language_, L"\u6253\u5f00\u7f51\u5740", L"Open website")};
    for (const auto* type : types) {
        SendMessageW(type_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(type));
    }
    SendMessageW(type_combo_, CB_SETMINVISIBLE, static_cast<WPARAM>(types.size()), 0);
    target_label_ = create_label(tr(language_, L"\u76ee\u6807", L"Target"));
    target_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(target_identifier)), instance_, nullptr);
    browse_button_ = CreateWindowW(L"BUTTON", tr(language_, L"\u6d4f\u89c8...", L"Browse..."),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(browse_identifier)), instance_, nullptr);
    arguments_label_ = create_label(tr(language_, L"\u53c2\u6570\uff08\u53ef\u9009\uff09", L"Arguments (optional)"));
    arguments_edit_ = CreateWindowExW(WS_EX_STATICEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(arguments_identifier)), instance_, nullptr);
    administrator_checkbox_ = CreateWindowW(L"BUTTON",
        tr(language_, L"\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u8fd0\u884c", L"Run as administrator"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(administrator_identifier)), instance_, nullptr);
    const auto create_button = [&](const wchar_t* text, const int identifier) {
        return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
            instance_, nullptr);
    };
    add_category_button_ = create_button(tr(language_, L"\u6dfb\u52a0\u5206\u7c7b", L"Add category"), add_category_identifier);
    add_item_button_ = create_button(tr(language_, L"\u6dfb\u52a0\u542f\u52a8\u9879", L"Add item"), add_item_identifier);
    add_separator_button_ = create_button(tr(language_, L"\u6dfb\u52a0\u5206\u9694\u7ebf", L"Add separator"), add_separator_identifier);
    delete_button_ = create_button(tr(language_, L"\u5220\u9664", L"Delete"), delete_identifier);
    move_up_button_ = create_button(tr(language_, L"\u4e0a\u79fb", L"Move up"), move_up_identifier);
    move_down_button_ = create_button(tr(language_, L"\u4e0b\u79fb", L"Move down"), move_down_identifier);
    increase_level_button_ = create_button(tr(language_, L"\u5347\u7ea7", L"Promote"), increase_level_identifier);
    decrease_level_button_ = create_button(tr(language_, L"\u964d\u7ea7", L"Demote"), decrease_level_identifier);
    update_fonts();
    rebuild_tree();
}

void MenuEditorWindow::update_fonts() {
    const auto old_font = font_;
    const auto old_section_font = section_font_;
    font_ = CreateFontW(-MulDiv(13, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    section_font_ = CreateFontW(-MulDiv(16, dpi_, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    set_font(detail_title_, section_font_);
    const std::array controls{main_menu_button_, second_menu_button_, tree_, name_label_, name_edit_, access_key_label_,
        access_key_edit_, type_label_, type_combo_, target_label_, target_edit_,
        browse_button_, arguments_label_, arguments_edit_, administrator_checkbox_,
        add_item_button_, add_category_button_, add_separator_button_, delete_button_,
        move_up_button_, move_down_button_, decrease_level_button_, increase_level_button_};
    for (const auto control : controls) set_font(control, font_);
    TreeView_SetItemHeight(tree_, MulDiv(30, dpi_, 96));
    if (old_font) DeleteObject(old_font);
    if (old_section_font) DeleteObject(old_section_font);
}

void MenuEditorWindow::layout_controls(const int width, const int height) {
    const auto layout_dpi = std::min(dpi_, std::max(dpi_ * 2 / 3,
        static_cast<UINT>(std::max(1, height) * 96 / 500)));
    const auto scale = [layout_dpi](const int value) {
        return MulDiv(value, layout_dpi, 96);
    };
    const auto margin = scale(2);
    TreeView_SetItemHeight(tree_, scale(30));
    const auto segment_width = scale(126);
    const auto segment_height = scale(36);
    MoveWindow(main_menu_button_, margin, margin, segment_width, segment_height, TRUE);
    MoveWindow(second_menu_button_, margin + segment_width, margin,
               segment_width, segment_height, TRUE);

    const auto content_top = margin + segment_height + scale(16);
    const auto content_bottom = height - margin;
    const auto left_x = margin;
    const auto content_right = width - margin;
    const auto splitter_width = scale(8);
    const auto minimum_left = scale(280);
    const auto minimum_right = scale(330);
    if (splitter_x_ == 0) {
        splitter_x_ = left_x + (content_right - left_x) * 42 / 100;
    }
    splitter_x_ = std::clamp(splitter_x_, left_x + minimum_left,
        content_right - splitter_width - scale(16) - minimum_right);
    const auto left_width = splitter_x_ - left_x;
    const auto right_x = splitter_x_ + splitter_width + scale(16);
    const auto right_width = content_right - right_x;
    const auto gap = scale(8);
    const auto button_height = scale(36);
    const auto add_width = (left_width - gap * 2) / 3;
    MoveWindow(add_category_button_, left_x, content_top, add_width, button_height, TRUE);
    MoveWindow(add_item_button_, left_x + add_width + gap, content_top,
               add_width, button_height, TRUE);
    MoveWindow(add_separator_button_, left_x + (add_width + gap) * 2, content_top,
               add_width, button_height, TRUE);

    const auto structure_top = content_bottom - button_height;
    const auto structure_width = (left_width - gap * 3) / 4;
    MoveWindow(tree_, left_x, content_top + button_height + scale(12), left_width,
               structure_top - content_top - button_height - scale(24), TRUE);
    MoveWindow(move_up_button_, left_x, structure_top, structure_width, button_height, TRUE);
    MoveWindow(move_down_button_, left_x + structure_width + gap, structure_top,
               structure_width, button_height, TRUE);
    MoveWindow(increase_level_button_, left_x + (structure_width + gap) * 2,
               structure_top, structure_width, button_height, TRUE);
    MoveWindow(decrease_level_button_, left_x + (structure_width + gap) * 3,
               structure_top, structure_width, button_height, TRUE);
    MoveWindow(splitter_, splitter_x_, content_top, splitter_width,
               content_bottom - content_top, TRUE);

    MoveWindow(detail_title_, right_x, content_top, right_width, scale(30), TRUE);
    const auto field_height = scale(34);
    const auto browse_width = scale(92);
    auto row_y = content_top + scale(42);
    const auto place_field = [&](const HWND label, const HWND control, const HWND browse) {
        MoveWindow(label, right_x, row_y, right_width, scale(22), TRUE);
        row_y += scale(24);
        MoveWindow(control, right_x, row_y,
            right_width - (browse ? browse_width + scale(8) : 0), field_height, TRUE);
        if (browse) {
            MoveWindow(browse, right_x + right_width - browse_width, row_y,
                       browse_width, field_height, TRUE);
        }
        row_y += field_height + scale(12);
    };
    place_field(name_label_, name_edit_, nullptr);
    place_field(access_key_label_, access_key_edit_, nullptr);
    MoveWindow(type_label_, right_x, row_y, right_width, scale(22), TRUE);
    row_y += scale(24);
    MoveWindow(type_combo_, right_x, row_y, right_width, scale(180), TRUE);
    row_y += field_height + scale(12);
    place_field(target_label_, target_edit_, browse_button_);
    place_field(arguments_label_, arguments_edit_, nullptr);
    MoveWindow(administrator_checkbox_, right_x, row_y, right_width, scale(32), TRUE);
    MoveWindow(delete_button_, right_x, content_bottom - button_height,
               scale(104), button_height, TRUE);
}

void MenuEditorWindow::rebuild_tree(MenuElement* selection) {
    SendMessageW(tree_, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(tree_);
    auto& root = *documents_[static_cast<std::size_t>(active_menu_)]->root;
    insert_tree_children(TVI_ROOT, root);
    auto selected_item = static_cast<HTREEITEM>(nullptr);
    HTREEITEM item = TreeView_GetRoot(tree_);
    while (item) {
        TVITEMW value{.mask = TVIF_PARAM, .hItem = item};
        if (TreeView_GetItem(tree_, &value)
            && reinterpret_cast<MenuElement*>(value.lParam) == selection) {
            selected_item = item;
            break;
        }
        auto next = TreeView_GetChild(tree_, item);
        if (!next) {
            while (item && !TreeView_GetNextSibling(tree_, item)) {
                item = TreeView_GetParent(tree_, item);
            }
            next = item ? TreeView_GetNextSibling(tree_, item) : nullptr;
        }
        item = next;
    }
    if (!selected_item) selected_item = TreeView_GetRoot(tree_);
    TreeView_SelectItem(tree_, selected_item);
    if (selected_item) TreeView_EnsureVisible(tree_, selected_item);
    SendMessageW(tree_, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(tree_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    update_selection();
}

void MenuEditorWindow::insert_tree_children(
    const HTREEITEM parent_item, MenuCategory& category) {
    for (auto& child : category.children) {
        auto* element = child.get();
        auto label = tree_label(*element);
        TVINSERTSTRUCTW insertion{};
        insertion.hParent = parent_item;
        insertion.hInsertAfter = TVI_LAST;
        insertion.item.mask = TVIF_TEXT | TVIF_PARAM;
        insertion.item.pszText = label.data();
        insertion.item.lParam = reinterpret_cast<LPARAM>(element);
        const auto tree_item = TreeView_InsertItem(tree_, &insertion);
        if (auto* nested = dynamic_cast<MenuCategory*>(element)) {
            insert_tree_children(tree_item, *nested);
            TreeView_Expand(tree_, tree_item, TVE_EXPAND);
        }
    }
}

std::wstring MenuEditorWindow::tree_label(const MenuElement& element) const {
    std::wstring label;
    std::optional<wchar_t> access_key;
    if (const auto* category = dynamic_cast<const MenuCategory*>(&element)) {
        label = category->name;
        access_key = category->access_key;
    } else if (const auto* entry = dynamic_cast<const MenuEntry*>(&element)) {
        label = entry->display_name;
        access_key = entry->access_key;
    } else {
        return L"----------------";
    }
    if (access_key) {
        label.append(L"  [");
        label.push_back(*access_key);
        label.push_back(L']');
    }
    return label;
}

bool MenuEditorWindow::has_duplicate_access_key(const MenuElement* element) const {
    const auto location = find_location(const_cast<MenuElement*>(element));
    if (!location) return false;
    const auto key_for = [](const MenuElement* candidate) -> std::optional<wchar_t> {
        if (const auto* category = dynamic_cast<const MenuCategory*>(candidate)) {
            return category->access_key;
        }
        if (const auto* entry = dynamic_cast<const MenuEntry*>(candidate)) {
            return entry->access_key;
        }
        return std::nullopt;
    };
    const auto key = key_for(element);
    if (!key) return false;
    const auto normalized = towupper(*key);
    return std::ranges::count_if(location->parent->children,
        [&](const auto& child) {
            const auto candidate = key_for(child.get());
            return candidate && towupper(*candidate) == normalized;
        }) > 1;
}

void MenuEditorWindow::update_tree_item(MenuElement* element) {
    auto item = TreeView_GetRoot(tree_);
    while (item) {
        TVITEMW value{.mask = TVIF_PARAM, .hItem = item};
        if (TreeView_GetItem(tree_, &value)
            && reinterpret_cast<MenuElement*>(value.lParam) == element) {
            auto label = tree_label(*element);
            value.mask = TVIF_TEXT;
            value.pszText = label.data();
            TreeView_SetItem(tree_, &value);
            InvalidateRect(tree_, nullptr, TRUE);
            return;
        }
        auto next = TreeView_GetChild(tree_, item);
        if (!next) {
            while (item && !TreeView_GetNextSibling(tree_, item)) {
                item = TreeView_GetParent(tree_, item);
            }
            next = item ? TreeView_GetNextSibling(tree_, item) : nullptr;
        }
        item = next;
    }
}

MenuElement* MenuEditorWindow::selected_element() const {
    const auto item = TreeView_GetSelection(tree_);
    if (!item) return nullptr;
    TVITEMW value{.mask = TVIF_PARAM, .hItem = item};
    return TreeView_GetItem(tree_, &value)
        ? reinterpret_cast<MenuElement*>(value.lParam) : nullptr;
}

std::optional<MenuEditorWindow::ElementLocation> MenuEditorWindow::find_location(
    MenuElement* element) const {
    if (!element) return std::nullopt;
    return find_location_in(*documents_[static_cast<std::size_t>(active_menu_)]->root, element);
}

std::pair<MenuCategory*, std::size_t> MenuEditorWindow::insertion_location() const {
    auto* selected = selected_element();
    if (auto* category = dynamic_cast<MenuCategory*>(selected)) {
        return {category, category->children.size()};
    }
    if (const auto location = find_location(selected)) {
        return {location->parent, location->index + 1};
    }
    auto* root = documents_[static_cast<std::size_t>(active_menu_)]->root.get();
    return {root, root->children.size()};
}

void MenuEditorWindow::update_selection() {
    auto* selected = selected_element();
    const auto location = find_location(selected);
    const auto root = selected
        == documents_[static_cast<std::size_t>(active_menu_)]->root.get();
    auto* category = dynamic_cast<MenuCategory*>(selected);
    auto* entry = dynamic_cast<MenuEntry*>(selected);

    updating_details_ = true;
    SetWindowTextW(name_edit_, L"");
    SetWindowTextW(access_key_edit_, L"");
    SetWindowTextW(target_edit_, L"");
    SetWindowTextW(arguments_edit_, L"");
    SendMessageW(administrator_checkbox_, BM_SETCHECK, BST_UNCHECKED, 0);
    if (!selected) {
        SetWindowTextW(detail_title_,
            tr(language_, L"\u9009\u62e9\u4e00\u4e2a\u9879\u76ee\u4ee5\u7f16\u8f91", L"Select an item to edit"));
    } else if (root) {
        SetWindowTextW(detail_title_, active_menu_ == 0
            ? tr(language_, L"\u4e3b\u83dc\u5355", L"Main menu")
            : tr(language_, L"\u7b2c\u4e8c\u83dc\u5355", L"Second menu"));
    } else if (category) {
        SetWindowTextW(detail_title_, tr(language_, L"\u5206\u7c7b", L"Category"));
        SetWindowTextW(name_edit_, category->name.c_str());
        if (category->access_key) {
            const std::wstring access_key(1, *category->access_key);
            SetWindowTextW(access_key_edit_, access_key.c_str());
        }
    } else if (entry) {
        SetWindowTextW(detail_title_, tr(language_, L"\u542f\u52a8\u9879", L"Launch item"));
        SetWindowTextW(name_edit_, entry->display_name.c_str());
        if (entry->access_key) {
            const std::wstring access_key(1, *entry->access_key);
            SetWindowTextW(access_key_edit_, access_key.c_str());
        }
        const auto type = item_type(*entry);
        SendMessageW(type_combo_, CB_SETCURSEL, type, 0);
        if (type == 3) {
            SetWindowTextW(target_edit_, entry->value.c_str());
        } else if (const auto parsed = ParsedCommand::try_parse(entry->value)) {
            SetWindowTextW(target_edit_, parsed->executable.c_str());
            if (type == 0) SetWindowTextW(arguments_edit_, parsed->arguments.c_str());
        } else {
            SetWindowTextW(target_edit_, entry->value.c_str());
        }
        SendMessageW(administrator_checkbox_, BM_SETCHECK,
            entry->run_as_administrator ? BST_CHECKED : BST_UNCHECKED, 0);
    } else {
        SetWindowTextW(detail_title_, tr(language_, L"\u5206\u9694\u7ebf", L"Separator"));
    }
    updating_details_ = false;
    update_type_controls();

    EnableWindow(delete_button_, selected && !root ? TRUE : FALSE);
    EnableWindow(move_up_button_, location && location->index > 0 ? TRUE : FALSE);
    EnableWindow(move_down_button_, location
        && location->index + 1 < location->parent->children.size() ? TRUE : FALSE);
    const auto parent_location = location ? find_location(location->parent) : std::nullopt;
    const auto can_increase_level = location && location->index > 0
        && dynamic_cast<MenuCategory*>(location->parent->children[location->index - 1].get());
    EnableWindow(increase_level_button_, parent_location ? TRUE : FALSE);
    EnableWindow(decrease_level_button_, can_increase_level ? TRUE : FALSE);
}

void MenuEditorWindow::update_type_controls() {
    const auto entry = dynamic_cast<MenuEntry*>(selected_element()) != nullptr;
    const auto category = dynamic_cast<MenuCategory*>(selected_element()) != nullptr
        && selected_element()
            != documents_[static_cast<std::size_t>(active_menu_)]->root.get();
    for (const auto control : {name_label_, name_edit_, access_key_label_, access_key_edit_}) {
        ShowWindow(control, entry || category ? SW_SHOW : SW_HIDE);
    }
    for (const auto control : {type_label_, type_combo_, target_label_, target_edit_}) {
        ShowWindow(control, entry ? SW_SHOW : SW_HIDE);
    }
    const auto type = std::max<LRESULT>(0, SendMessageW(type_combo_, CB_GETCURSEL, 0, 0));
    const std::array<const wchar_t*, 4> target_labels{
        tr(language_, L"\u5e94\u7528\u8def\u5f84", L"Application path"),
        tr(language_, L"\u6587\u4ef6\u5939\u8def\u5f84", L"Folder path"),
        tr(language_, L"\u6587\u4ef6\u8def\u5f84", L"File path"),
        tr(language_, L"\u7f51\u5740", L"Website URL")};
    SetWindowTextW(target_label_, target_labels[static_cast<std::size_t>(type)]);
    const auto application = entry && type == 0;
    ShowWindow(arguments_label_, application ? SW_SHOW : SW_HIDE);
    ShowWindow(arguments_edit_, application ? SW_SHOW : SW_HIDE);
    ShowWindow(administrator_checkbox_, application ? SW_SHOW : SW_HIDE);
    ShowWindow(browse_button_, entry && type != 3 ? SW_SHOW : SW_HIDE);
}

void MenuEditorWindow::apply_detail_changes() {
    if (updating_details_) return;
    auto* selected = selected_element();
    auto* root = documents_[static_cast<std::size_t>(active_menu_)]->root.get();
    if (!selected || selected == root) return;

    const auto access_key_text = control_text(access_key_edit_);
    const auto access_key = access_key_text.empty()
        ? std::optional<wchar_t>{} : std::optional<wchar_t>{access_key_text.front()};
    if (auto* category = dynamic_cast<MenuCategory*>(selected)) {
        category->name = control_text(name_edit_);
        category->access_key = access_key;
    } else if (auto* entry = dynamic_cast<MenuEntry*>(selected)) {
        entry->display_name = control_text(name_edit_);
        entry->access_key = access_key;
        const auto type = std::max<LRESULT>(0, SendMessageW(type_combo_, CB_GETCURSEL, 0, 0));
        const auto target = control_text(target_edit_);
        const auto arguments = type == 0 ? control_text(arguments_edit_) : std::wstring{};
        entry->kind = type == 3 ? MenuEntryKind::web : MenuEntryKind::command;
        entry->value = type == 3 ? target : ParsedCommand{target, arguments}.with_executable(target);
        entry->run_as_administrator = type == 0
            && SendMessageW(administrator_checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        entry->resolved_value.reset();
    } else {
        return;
    }
    mark_dirty();
    update_tree_item(selected);
}

void MenuEditorWindow::browse_target() {
    const auto type = std::max<LRESULT>(0, SendMessageW(type_combo_, CB_GETCURSEL, 0, 0));
    if (type == 1) {
        BROWSEINFOW information{
            .hwndOwner = window_,
            .lpszTitle = tr(language_, L"\u9009\u62e9\u6587\u4ef6\u5939", L"Choose folder"),
            .ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE,
        };
        const auto item = SHBrowseForFolderW(&information);
        if (!item) return;
        std::array<wchar_t, MAX_PATH> path{};
        if (SHGetPathFromIDListW(item, path.data())) SetWindowTextW(target_edit_, path.data());
        CoTaskMemFree(item);
        return;
    }
    std::array<wchar_t, 32768> path{};
    const auto current = control_text(target_edit_);
    wcsncpy_s(path.data(), path.size(), current.c_str(), _TRUNCATE);
    const wchar_t application_filter[] =
        L"Applications (*.exe;*.lnk;*.cmd;*.bat)\0*.exe;*.lnk;*.cmd;*.bat\0All files (*.*)\0*.*\0\0";
    const wchar_t file_filter[] = L"All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{
        .lStructSize = sizeof(dialog),
        .hwndOwner = window_,
        .lpstrFilter = type == 0 ? application_filter : file_filter,
        .lpstrFile = path.data(),
        .nMaxFile = static_cast<DWORD>(path.size()),
        .Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR,
        .lpstrDefExt = type == 0 ? L"exe" : nullptr,
    };
    if (GetOpenFileNameW(&dialog)) SetWindowTextW(target_edit_, path.data());
}

void MenuEditorWindow::add_item() {
    auto [parent, index] = insertion_location();
    auto item = std::make_unique<MenuEntry>(
        tr(language_, L"\u65b0\u542f\u52a8\u9879", L"New launch item"), L"",
        MenuEntryKind::command, 0, false);
    auto* selection = item.get();
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(index),
                            std::move(item));
    mark_dirty();
    rebuild_tree(selection);
    SetFocus(name_edit_);
    SendMessageW(name_edit_, EM_SETSEL, 0, -1);
}

void MenuEditorWindow::add_category() {
    auto [parent, index] = insertion_location();
    auto category = std::make_unique<MenuCategory>(
        tr(language_, L"\u65b0\u5206\u7c7b", L"New category"));
    auto* selection = category.get();
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(index),
                            std::move(category));
    mark_dirty();
    rebuild_tree(selection);
    SetFocus(name_edit_);
    SendMessageW(name_edit_, EM_SETSEL, 0, -1);
}

void MenuEditorWindow::add_separator() {
    auto [parent, index] = insertion_location();
    auto separator = std::make_unique<MenuSeparator>();
    auto* selection = separator.get();
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(index),
                            std::move(separator));
    mark_dirty();
    rebuild_tree(selection);
}

void MenuEditorWindow::delete_selected() {
    auto* selected = selected_element();
    const auto location = find_location(selected);
    if (!location) return;
    const auto* category = dynamic_cast<const MenuCategory*>(selected);
    if (category && !category->children.empty()
        && MessageBoxW(window_,
            tr(language_, L"\u5220\u9664\u8be5\u5206\u7c7b\u4e5f\u4f1a\u5220\u9664\u5176\u4e2d\u7684\u6240\u6709\u5185\u5bb9\u3002\u662f\u5426\u7ee7\u7eed\uff1f",
               L"Deleting this category also deletes everything inside it. Continue?"),
            product_name, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    auto* parent = location->parent;
    parent->children.erase(parent->children.begin()
        + static_cast<std::ptrdiff_t>(location->index));
    mark_dirty();
    rebuild_tree(parent);
}

void MenuEditorWindow::move_selected(const int direction) {
    auto* selected = selected_element();
    const auto location = find_location(selected);
    if (!location) return;
    const auto destination = static_cast<std::ptrdiff_t>(location->index) + direction;
    if (destination < 0
        || destination >= static_cast<std::ptrdiff_t>(location->parent->children.size())) return;
    std::swap(location->parent->children[location->index],
              location->parent->children[static_cast<std::size_t>(destination)]);
    mark_dirty();
    rebuild_tree(selected);
}

void MenuEditorWindow::change_selected_level(const int direction) {
    auto* selected = selected_element();
    const auto location = find_location(selected);
    if (!location) return;
    if (direction > 0) {
        if (location->index == 0) return;
        auto* destination = dynamic_cast<MenuCategory*>(
            location->parent->children[location->index - 1].get());
        if (!destination) return;
        auto moving = std::move(location->parent->children[location->index]);
        location->parent->children.erase(location->parent->children.begin()
            + static_cast<std::ptrdiff_t>(location->index));
        destination->children.push_back(std::move(moving));
    } else {
        const auto parent_location = find_location(location->parent);
        if (!parent_location) return;
        auto moving = std::move(location->parent->children[location->index]);
        location->parent->children.erase(location->parent->children.begin()
            + static_cast<std::ptrdiff_t>(location->index));
        parent_location->parent->children.insert(
            parent_location->parent->children.begin()
                + static_cast<std::ptrdiff_t>(parent_location->index + 1),
            std::move(moving));
    }
    mark_dirty();
    rebuild_tree(selected);
}

MenuElement* MenuEditorWindow::first_invalid_element(
    MenuCategory& category, const bool root) const {
    const auto contains_line_break = [](const std::wstring& value) {
        return value.find_first_of(L"\r\n") != std::wstring::npos;
    };
    if (!root) {
        const auto normalized = trim(category.name);
        if (normalized.empty() || normalized.front() == L'-'
            || normalized != category.name || contains_line_break(category.name)
            || (category.access_key && !is_valid_menu_access_key(*category.access_key))) {
            return &category;
        }
    }
    for (const auto& child : category.children) {
        if (auto* nested = dynamic_cast<MenuCategory*>(child.get())) {
            if (auto* invalid = first_invalid_element(*nested, false)) return invalid;
            continue;
        }
        auto* entry = dynamic_cast<MenuEntry*>(child.get());
        if (!entry) continue;
        if (entry->display_name.empty() || contains_line_break(entry->display_name)
            || entry->display_name.find(L'|') != std::wstring::npos
            || entry->value.empty() || contains_line_break(entry->value)
            || (entry->access_key && !is_valid_menu_access_key(*entry->access_key))
            || (!entry->run_as_administrator && entry->display_name.ends_with(L"[#]"))
            || (entry->kind == MenuEntryKind::web && !web_url(entry->value))) {
            return entry;
        }
    }
    return nullptr;
}

bool MenuEditorWindow::apply() {
    if (!dirty_) return true;
    for (std::size_t index = 0; index < documents_.size(); ++index) {
        if (auto* invalid = first_invalid_element(*documents_[index]->root, true)) {
            active_menu_ = static_cast<int>(index);
            SendMessageW(main_menu_button_, BM_SETCHECK,
                         active_menu_ == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(second_menu_button_, BM_SETCHECK,
                         active_menu_ == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
            rebuild_tree(invalid);
            MessageBoxW(window_,
                tr(language_, L"\u83dc\u5355\u5305\u542b\u65e0\u6548\u7684\u540d\u79f0\u6216\u76ee\u6807\uff0c\u65e0\u6cd5\u4fdd\u5b58\u3002",
                   L"The menu contains an invalid name or target and cannot be saved."),
                product_name, MB_OK | MB_ICONWARNING);
            SetFocus(dynamic_cast<MenuEntry*>(invalid) ? target_edit_ : name_edit_);
            return false;
        }
    }
    std::array<std::optional<std::wstring>, 2> sources_before_apply;
    auto changed_externally = false;
    try {
        for (std::size_t index = 0; index < paths_.size(); ++index) {
            sources_before_apply[index] = read_source(paths_[index]);
            changed_externally = changed_externally
                || sources_before_apply[index] != original_sources_[index];
        }
    } catch (...) {
        changed_externally = true;
    }
    if (changed_externally
        && MessageBoxW(window_,
            tr(language_,
               L"\u7f16\u8f91\u671f\u95f4\u914d\u7f6e\u6587\u4ef6\u5df2\u88ab\u5176\u4ed6\u7a0b\u5e8f\u4fee\u6539\u3002\u662f\u5426\u4ecd\u8981\u8986\u76d6\u8fd9\u4e9b\u66f4\u6539\uff1f",
               L"The configuration files were changed by another program while the editor was open. Overwrite those changes?"),
            product_name, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
    std::array<bool, 2> written{};
    try {
        const auto main_text = MenuWriter::serialize(*documents_[0]);
        const auto second_text = MenuWriter::serialize(*documents_[1]);
        write_configuration_text(paths_[0], main_text);
        written[0] = true;
        if (original_sources_[1] || !documents_[1]->root->children.empty()) {
            write_configuration_text(paths_[1], second_text);
            written[1] = true;
        }
        original_sources_[0] = main_text;
        if (original_sources_[1] || !documents_[1]->root->children.empty()) {
            original_sources_[1] = second_text;
        }
        diagnose(L"menu editor saved configuration");
        dirty_ = false;
        return true;
    } catch (const std::exception&) {
        for (std::size_t reverse = paths_.size(); reverse > 0; --reverse) {
            const auto index = reverse - 1;
            if (!written[index]) continue;
            try {
                if (sources_before_apply[index]) {
                    write_configuration_text(paths_[index], *sources_before_apply[index]);
                } else {
                    std::error_code rollback_error;
                    std::filesystem::remove(paths_[index], rollback_error);
                }
            } catch (...) {
                diagnose(L"menu editor rollback failed");
            }
        }
        diagnose(L"menu editor save failed");
        MessageBoxW(window_,
            tr(language_, L"\u65e0\u6cd5\u4fdd\u5b58\u83dc\u5355\u914d\u7f6e\u3002\u8bf7\u67e5\u770b Log\\Simpilot.log\u3002",
               L"The menu configuration could not be saved. See Log\\Simpilot.log."),
            product_name, MB_OK | MB_ICONERROR);
        return false;
    }
}

void MenuEditorWindow::mark_dirty() {
    if (dirty_) return;
    dirty_ = true;
    if (dirty_sink_) dirty_sink_();
}

std::optional<std::wstring> MenuEditorWindow::read_source(
    const std::filesystem::path& path) const {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return std::nullopt;
    return read_configuration_text(path);
}

void MenuEditorWindow::diagnose(const std::wstring_view message) const noexcept {
    if (!diagnostic_sink_) return;
    try {
        diagnostic_sink_(message);
    } catch (...) {
    }
}

LRESULT CALLBACK MenuEditorWindow::window_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* editor = static_cast<MenuEditorWindow*>(creation->lpCreateParams);
        editor->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
    }
    auto* editor = reinterpret_cast<MenuEditorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return editor ? editor->handle_message(message, wparam, lparam)
                   : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK MenuEditorWindow::splitter_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam,
    const UINT_PTR subclass_identifier, const DWORD_PTR reference_data) {
    auto* editor = reinterpret_cast<MenuEditorWindow*>(reference_data);
    if (message == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return TRUE;
    }
    if (message == WM_LBUTTONDOWN) {
        SetCapture(window);
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return 0;
    }
    if (message == WM_MOUSEMOVE && GetCapture() == window && editor) {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        MapWindowPoints(window, editor->window_, &point, 1);
        RECT splitter_rectangle{};
        GetClientRect(window, &splitter_rectangle);
        editor->splitter_x_ = point.x
            - (splitter_rectangle.right - splitter_rectangle.left) / 2;
        RECT client{};
        GetClientRect(editor->window_, &client);
        editor->layout_controls(client.right, client.bottom);
        return 0;
    }
    if (message == WM_LBUTTONUP && GetCapture() == window) {
        ReleaseCapture();
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        RECT rectangle{};
        GetClientRect(window, &rectangle);
        FillRect(dc, &rectangle, settings_visual_style::background_brush());
        const auto pen = CreatePen(PS_SOLID, 1,
            settings_visual_style::high_contrast_enabled()
                ? GetSysColor(COLOR_3DSHADOW) : RGB(208, 208, 208));
        const auto old_pen = SelectObject(dc, pen);
        const auto x = (rectangle.right - rectangle.left) / 2;
        MoveToEx(dc, x, rectangle.top, nullptr);
        LineTo(dc, x, rectangle.bottom);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, &MenuEditorWindow::splitter_procedure,
                             subclass_identifier);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CALLBACK MenuEditorWindow::tree_procedure(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam,
    const UINT_PTR subclass_identifier, const DWORD_PTR reference_data) {
    auto* editor = reinterpret_cast<MenuEditorWindow*>(reference_data);
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && editor) {
        if (wparam == VK_DELETE) {
            editor->delete_selected();
            return 0;
        }
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            if (wparam == VK_UP || wparam == VK_DOWN) {
                editor->move_selected(wparam == VK_UP ? -1 : 1);
                return 0;
            }
            if (wparam == VK_LEFT || wparam == VK_RIGHT) {
                editor->change_selected_level(wparam == VK_LEFT ? -1 : 1);
                return 0;
            }
        }
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, &MenuEditorWindow::tree_procedure,
                             subclass_identifier);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT MenuEditorWindow::draw_tree_item(NMTVCUSTOMDRAW& drawing) const {
    if (drawing.nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (drawing.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        const auto* element = reinterpret_cast<const MenuElement*>(drawing.nmcd.lItemlParam);
        return has_duplicate_access_key(element) ? CDRF_NOTIFYPOSTPAINT : CDRF_DODEFAULT;
    }
    if (drawing.nmcd.dwDrawStage != CDDS_ITEMPOSTPAINT) return CDRF_DODEFAULT;
    const auto* element = reinterpret_cast<const MenuElement*>(drawing.nmcd.lItemlParam);
    if (!element || !has_duplicate_access_key(element)) return CDRF_DODEFAULT;
    const auto label = tree_label(*element);
    if (label.size() < 3) return CDRF_DODEFAULT;

    RECT rectangle{};
    const auto item = reinterpret_cast<HTREEITEM>(drawing.nmcd.dwItemSpec);
    if (!TreeView_GetItemRect(tree_, item, &rectangle, TRUE)) return CDRF_DODEFAULT;
    const auto prefix = label.substr(0, label.size() - 2);
    SIZE prefix_size{};
    SIZE key_size{};
    GetTextExtentPoint32W(drawing.nmcd.hdc, prefix.c_str(),
                          static_cast<int>(prefix.size()), &prefix_size);
    GetTextExtentPoint32W(drawing.nmcd.hdc, &label[label.size() - 2], 1, &key_size);
    RECT key_rectangle{
        rectangle.left + prefix_size.cx - 1, rectangle.top,
        rectangle.left + prefix_size.cx + key_size.cx + 1, rectangle.bottom};
        const auto brush = CreateSolidBrush(RGB(253, 231, 233));
    FillRect(drawing.nmcd.hdc, &key_rectangle, brush);
    DeleteObject(brush);
    const auto old_background = SetBkMode(drawing.nmcd.hdc, TRANSPARENT);
    const auto old_color = SetTextColor(drawing.nmcd.hdc, RGB(32, 32, 32));
    DrawTextW(drawing.nmcd.hdc, &label[label.size() - 2], 1, &key_rectangle,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    SetTextColor(drawing.nmcd.hdc, old_color);
    SetBkMode(drawing.nmcd.hdc, old_background);
    return CDRF_DODEFAULT;
}

LRESULT MenuEditorWindow::handle_message(
    const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_ERASEBKGND) {
        return settings_visual_style::erase_background(window_, wparam);
    }
    if (settings_visual_style::is_color_message(message)) {
        return settings_visual_style::handle_color_message(message, wparam, lparam);
    }
    if (message == WM_CREATE) {
        create_controls();
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        layout_controls(rectangle.right, rectangle.bottom);
        return 0;
    }
    if (message == WM_SIZE) {
        layout_controls(LOWORD(lparam), HIWORD(lparam));
        return 0;
    }
    if (message == WM_DPICHANGED) {
        const auto old_dpi = dpi_;
        dpi_ = HIWORD(wparam);
        if (splitter_x_ > 0 && old_dpi > 0) {
            splitter_x_ = MulDiv(splitter_x_, dpi_, old_dpi);
        }
        update_fonts();
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        layout_controls(rectangle.right, rectangle.bottom);
        return 0;
    }
    if (message == WM_NOTIFY) {
        const auto* notification = reinterpret_cast<const NMHDR*>(lparam);
        if (notification && notification->hwndFrom == tree_) {
            if (notification->code == NM_CUSTOMDRAW) {
                return draw_tree_item(*reinterpret_cast<NMTVCUSTOMDRAW*>(lparam));
            }
            if (notification->code == TVN_SELCHANGEDW) {
                update_selection();
                return 0;
            }
        }
    }
    if (message == WM_COMMAND) {
        const auto identifier = LOWORD(wparam);
        const auto notification = HIWORD(wparam);
        if ((identifier == name_identifier || identifier == access_key_identifier
             || identifier == target_identifier || identifier == arguments_identifier)
            && notification == EN_CHANGE) {
            apply_detail_changes();
            return 0;
        }
        if (identifier == type_identifier && notification == CBN_SELCHANGE) {
            update_type_controls();
            apply_detail_changes();
            return 0;
        }
        if (identifier == administrator_identifier && notification == BN_CLICKED) {
            apply_detail_changes();
            return 0;
        }
        if (identifier == browse_identifier && notification == BN_CLICKED) {
            browse_target();
            return 0;
        }
        if (notification != BN_CLICKED) return 0;
        switch (identifier) {
        case main_menu_identifier:
        case second_menu_identifier:
            active_menu_ = identifier == main_menu_identifier ? 0 : 1;
            rebuild_tree();
            return 0;
        case add_item_identifier: add_item(); return 0;
        case add_category_identifier: add_category(); return 0;
        case add_separator_identifier: add_separator(); return 0;
        case delete_identifier: delete_selected(); return 0;
        case move_up_identifier: move_selected(-1); return 0;
        case move_down_identifier: move_selected(1); return 0;
        case decrease_level_identifier: change_selected_level(1); return 0;
        case increase_level_identifier: change_selected_level(-1); return 0;
        default: break;
        }
    }
    if (message == WM_DESTROY) {
        window_ = nullptr;
        return 0;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

} // namespace simpilot
