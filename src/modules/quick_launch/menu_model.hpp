#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace simpilot {

[[nodiscard]] constexpr bool is_valid_menu_access_key(const wchar_t key) noexcept {
    return key != L'\0' && key != L'&' && key != L'(' && key != L')'
        && key != L'\r' && key != L'\n';
}

enum class MenuEntryKind {
    command,
    web,
};

struct MenuElement {
    virtual ~MenuElement() = default;
};

struct MenuCategory final : MenuElement {
    explicit MenuCategory(std::wstring name);

    std::wstring name;
    std::optional<wchar_t> access_key;
    std::vector<std::unique_ptr<MenuElement>> children;
};

struct MenuEntry final : MenuElement {
    MenuEntry(std::wstring display_name, std::wstring value, MenuEntryKind kind,
              int source_line, bool run_as_administrator = false);

    [[nodiscard]] const std::wstring& effective_value() const;

    std::wstring display_name;
    std::wstring value;
    std::optional<wchar_t> access_key;
    MenuEntryKind kind;
    int source_line;
    bool run_as_administrator;
    std::optional<std::wstring> resolved_value;
    bool is_available = true;
    std::optional<std::wstring> unavailable_reason;
};

struct MenuSeparator final : MenuElement {};

struct MenuDocument {
    std::unique_ptr<MenuCategory> root;

    [[nodiscard]] std::vector<MenuEntry*> entries();
};

} // namespace simpilot
