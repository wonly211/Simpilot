#pragma once

#include <optional>
#include <string>

namespace simpilot {

struct ParsedCommand {
    std::wstring executable;
    std::wstring arguments;

    [[nodiscard]] static std::optional<ParsedCommand> try_parse(const std::wstring& command_line);
    [[nodiscard]] std::wstring with_executable(const std::wstring& replacement) const;
};

} // namespace simpilot

