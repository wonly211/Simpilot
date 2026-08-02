#include "simpilot/menu_parser.hpp"

#include "simpilot/command.hpp"
#include "simpilot/config_file.hpp"

#include <algorithm>
#include <cwctype>
#include <map>
#include <sstream>
#include <stdexcept>

namespace simpilot {
namespace {

std::wstring trim(std::wstring value) {
    const auto first = std::find_if_not(value.begin(), value.end(), iswspace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), iswspace).base();
    return first < last ? std::wstring(first, last) : std::wstring{};
}

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

std::pair<std::wstring, std::optional<std::wstring>> split_once(
    const std::wstring& input, const wchar_t separator) {
    const auto index = input.find(separator);
    if (index == std::wstring::npos) {
        return {trim(input), std::nullopt};
    }
    return {trim(input.substr(0, index)), trim(input.substr(index + 1))};
}

std::pair<std::wstring, bool> parse_administrator_marker(const std::wstring& label) {
    auto result = trim(label);
    if (!result.ends_with(L"[#]")) {
        return {label, false};
    }
    result.resize(result.size() - 3);
    return {trim(std::move(result)), true};
}

std::pair<std::wstring, std::optional<wchar_t>> parse_access_key(std::wstring label) {
    label = trim(std::move(label));
    if (label.size() < 4 || label[label.size() - 4] != L'('
        || label[label.size() - 3] != L'&' || label.back() != L')') {
        return {std::move(label), std::nullopt};
    }
    const auto key = label[label.size() - 2];
    if (!is_valid_menu_access_key(key)) {
        return {std::move(label), std::nullopt};
    }
    label.resize(label.size() - 4);
    return {trim(std::move(label)), key};
}

std::pair<MenuEntryKind, std::wstring> parse_kind(std::wstring value) {
    value = trim(std::move(value));
    const auto normalized = lowercase(value);
    if (normalized.starts_with(L"http://") || normalized.starts_with(L"https://")) {
        return {MenuEntryKind::web, std::move(value)};
    }
    return {MenuEntryKind::command, std::move(value)};
}

std::wstring derive_display_name(const std::wstring& value) {
    const auto parsed = ParsedCommand::try_parse(value);
    if (!parsed) {
        return value;
    }
    auto file_name = std::filesystem::path(parsed->executable).filename().wstring();
    const auto extension = lowercase(std::filesystem::path(file_name).extension().wstring());
    static constexpr std::wstring_view extensions[] = {
        L".exe", L".lnk", L".bat", L".cmd", L".vbs", L".ps1", L".ahk"};
    if (std::find(std::begin(extensions), std::end(extensions), extension) != std::end(extensions)) {
        return std::filesystem::path(file_name).stem().wstring();
    }
    return file_name;
}

MenuCategory* find_parent(const std::map<int, MenuCategory*>& categories, int requested_depth) {
    for (int depth = std::max(0, requested_depth); depth >= 0; --depth) {
        if (const auto found = categories.find(depth); found != categories.end()) {
            return found->second;
        }
    }
    throw std::logic_error("Synthetic menu root is missing");
}

void collect_entries(MenuCategory& category, std::vector<MenuEntry*>& result) {
    for (auto& child : category.children) {
        if (auto* entry = dynamic_cast<MenuEntry*>(child.get())) {
            result.push_back(entry);
        } else if (auto* nested = dynamic_cast<MenuCategory*>(child.get())) {
            collect_entries(*nested, result);
        }
    }
}

} // namespace

MenuCategory::MenuCategory(std::wstring name_value) : name(std::move(name_value)) {}

MenuEntry::MenuEntry(std::wstring display_name_value, std::wstring value_value,
                     const MenuEntryKind kind_value, const int source_line_value,
                     const bool run_as_administrator_value)
    : display_name(std::move(display_name_value)), value(std::move(value_value)), kind(kind_value),
      source_line(source_line_value),
      run_as_administrator(run_as_administrator_value) {}

const std::wstring& MenuEntry::effective_value() const {
    return resolved_value ? *resolved_value : value;
}

std::vector<MenuEntry*> MenuDocument::entries() {
    std::vector<MenuEntry*> result;
    collect_entries(*root, result);
    return result;
}

MenuDocument MenuParser::parse_file(const std::filesystem::path& path) {
    return parse(read_configuration_text(path));
}

MenuDocument MenuParser::parse(const std::wstring& content) {
    auto root = std::make_unique<MenuCategory>(L"\u7b80\u9a6d | Simpilot");
    std::map<int, MenuCategory*> categories{{0, root.get()}};
    auto* current = root.get();

    std::wistringstream lines(content);
    std::wstring raw_line;
    int line_number = 0;
    while (std::getline(lines, raw_line)) {
        ++line_number;
        if (!raw_line.empty() && raw_line.back() == L'\r') {
            raw_line.pop_back();
        }
        const auto line = trim(raw_line);
        if (line.empty() || line.front() == L';') {
            continue;
        }
        if (line == L"|" || line == L"||") {
            current->children.push_back(std::make_unique<MenuSeparator>());
            continue;
        }

        const auto hyphen_count = static_cast<int>(
            std::distance(line.begin(), std::find_if(line.begin(), line.end(), [](wchar_t ch) { return ch != L'-'; })));
        if (hyphen_count > 0) {
            auto body = trim(line.substr(static_cast<std::size_t>(hyphen_count)));
            for (auto iterator = categories.lower_bound(hyphen_count); iterator != categories.end();) {
                iterator = categories.erase(iterator);
            }
            if (body.empty()) {
                current = find_parent(categories, hyphen_count - 1);
                current->children.push_back(std::make_unique<MenuSeparator>());
                continue;
            }

            auto* parent = find_parent(categories, hyphen_count - 1);
            auto [category_name, access_key] = parse_access_key(std::move(body));
            auto category = std::make_unique<MenuCategory>(std::move(category_name));
            category->access_key = access_key;
            current = category.get();
            parent->children.push_back(std::move(category));
            categories[hyphen_count] = current;
            continue;
        }

        auto [label_text, value_text] = split_once(line, L'|');
        auto [label_without_admin, run_as_admin] = parse_administrator_marker(label_text);
        auto [label, access_key] = parse_access_key(std::move(label_without_admin));
        if (access_key) {
            auto [label_without_access_admin, admin_after_access] =
                parse_administrator_marker(label);
            if (admin_after_access) {
                label = std::move(label_without_access_admin);
                run_as_admin = true;
            }
        }
        auto value = value_text.value_or(label);
        auto [kind, normalized_value] = parse_kind(std::move(value));
        auto display_name = label;
        if (!value_text) {
            display_name = derive_display_name(normalized_value);
        }
        auto entry = std::make_unique<MenuEntry>(
            trim(std::move(display_name)), std::move(normalized_value), kind,
            line_number, run_as_admin);
        entry->access_key = access_key;
        current->children.push_back(std::move(entry));
    }

    return MenuDocument{std::move(root)};
}

} // namespace simpilot
