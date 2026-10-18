#pragma once

#include <optional>
#include <string>

namespace simpilot {

class VariableExpander final {
public:
    explicit VariableExpander(std::wstring config_directory);

    [[nodiscard]] const std::wstring& config_directory() const noexcept;
    [[nodiscard]] std::wstring expand(const std::wstring& input) const;

private:
    [[nodiscard]] std::optional<std::wstring> resolve_variable(const std::wstring& name) const;
    std::wstring config_directory_;
};

} // namespace simpilot
