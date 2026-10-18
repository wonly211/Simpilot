#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace simpilot {

struct ParsedCommand {
    std::wstring executable;
    std::wstring arguments;

    [[nodiscard]] static std::optional<ParsedCommand> try_parse(const std::wstring& command_line);
    [[nodiscard]] std::wstring with_executable(const std::wstring& replacement) const;
};

[[nodiscard]] bool is_terminal_executable(std::wstring_view executable) noexcept;

} // namespace simpilot
