#include "simpilot/menu_writer.hpp"

#include "simpilot/config_file.hpp"

#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace simpilot {
namespace {

bool contains_line_break(const std::wstring& value) noexcept {
    return value.find_first_of(L"\r\n") != std::wstring::npos;
}

std::wstring trim(std::wstring value) {
    const auto first = std::find_if_not(value.begin(), value.end(), iswspace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), iswspace).base();
    return first < last ? std::wstring(first, last) : std::wstring{};
}

bool web_url(const std::wstring& value) {
    auto normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), towlower);
    return normalized.starts_with(L"http://") || normalized.starts_with(L"https://");
}

void append_access_key(std::wstring& value, const std::optional<wchar_t> access_key) {
    if (!access_key) return;
    value.append(L"(&");
    value.push_back(*access_key);
    value.push_back(L')');
}

std::optional<std::wstring> validate_category(const MenuCategory& category,
                                              const bool root) {
    const auto normalized_name = trim(category.name);
    if (!root && (normalized_name.empty() || normalized_name.front() == L'-'
                  || normalized_name != category.name
                  || contains_line_break(category.name)
                  || (category.access_key && !is_valid_menu_access_key(*category.access_key)))) {
        return L"Category names must be trimmed, non-empty, single-line text and cannot start with '-'.";
    }
    for (const auto& child : category.children) {
        if (const auto* nested = dynamic_cast<const MenuCategory*>(child.get())) {
            if (const auto error = validate_category(*nested, false)) return error;
            continue;
        }
        const auto* entry = dynamic_cast<const MenuEntry*>(child.get());
        if (!entry) continue;
        if (entry->display_name.empty() || contains_line_break(entry->display_name)
            || entry->display_name.find(L'|') != std::wstring::npos) {
            return L"Menu item names must be non-empty, single-line text and cannot contain '|'.";
        }
        if (entry->value.empty() || contains_line_break(entry->value)) {
            return L"Menu item targets must be non-empty single-line text.";
        }
        if (entry->access_key && !is_valid_menu_access_key(*entry->access_key)) {
            return L"Menu item access keys must contain one valid character.";
        }
        if (!entry->run_as_administrator && entry->display_name.ends_with(L"[#]")) {
            return L"A normal menu item name cannot end with '[#]'.";
        }
        if (entry->kind == MenuEntryKind::web && !web_url(entry->value)) {
            return L"Web menu items must use an HTTP or HTTPS URL.";
        }
    }
    return std::nullopt;
}

void write_category(const MenuCategory& category, const int depth,
                    int& current_depth, std::wstring& result) {
    for (const auto& child : category.children) {
        if (const auto* nested = dynamic_cast<const MenuCategory*>(child.get())) {
            result.append(static_cast<std::size_t>(depth + 1), L'-');
            result.append(nested->name);
            append_access_key(result, nested->access_key);
            result.append(L"\r\n");
            current_depth = depth + 1;
            write_category(*nested, depth + 1, current_depth, result);
            continue;
        }
        if (dynamic_cast<const MenuSeparator*>(child.get())) {
            if (current_depth == depth) {
                result.append(L"|\r\n");
            } else {
                result.append(static_cast<std::size_t>(depth + 1), L'-');
                result.append(L"\r\n");
                current_depth = depth;
            }
            continue;
        }
        const auto* entry = dynamic_cast<const MenuEntry*>(child.get());
        if (!entry) continue;
        if (current_depth != depth) {
            result.append(static_cast<std::size_t>(depth + 1), L'-');
            result.append(L"\r\n");
            current_depth = depth;
        }
        result.append(entry->display_name);
        append_access_key(result, entry->access_key);
        if (entry->run_as_administrator) result.append(L"[#]");
        result.push_back(L'|');
        result.append(entry->value).append(L"\r\n");
    }
}

} // namespace

std::optional<std::wstring> MenuWriter::validate(const MenuDocument& document) {
    if (!document.root) return L"The menu document has no root category.";
    return validate_category(*document.root, true);
}

std::wstring MenuWriter::serialize(const MenuDocument& document) {
    if (const auto error = validate(document)) {
        throw std::invalid_argument("Invalid menu document");
    }
    std::wstring result;
    int current_depth = 0;
    write_category(*document.root, 0, current_depth, result);
    return result;
}

void MenuWriter::save_file(const std::filesystem::path& path,
                           const MenuDocument& document) {
    write_configuration_text(path, serialize(document));
}

} // namespace simpilot
