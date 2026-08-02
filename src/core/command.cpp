#include "simpilot/command.hpp"

#include <algorithm>
#include <cwctype>
#include <regex>

namespace simpilot {
namespace {

std::wstring trim(std::wstring value) {
    const auto first = std::find_if_not(value.begin(), value.end(), iswspace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), iswspace).base();
    return first < last ? std::wstring(first, last) : std::wstring{};
}

std::wstring trim_left(std::wstring value) {
    const auto first = std::find_if_not(value.begin(), value.end(), iswspace);
    value.erase(value.begin(), first);
    return value;
}

} // namespace

std::optional<ParsedCommand> ParsedCommand::try_parse(const std::wstring& command_line) {
    auto value = trim(command_line);
    if (value.empty()) {
        return std::nullopt;
    }

    if (value.front() == L'\"') {
        const auto closing_quote = value.find(L'\"', 1);
        if (closing_quote > 1 && closing_quote != std::wstring::npos) {
            return ParsedCommand{value.substr(1, closing_quote - 1),
                                 trim_left(value.substr(closing_quote + 1))};
        }
    }

    static const std::wregex executable_pattern(
        LR"(^(.+?\.(?:exe|lnk|bat|cmd|vbs|ps1|ahk))(\s+.*|$))",
        std::regex_constants::icase | std::regex_constants::ECMAScript);
    std::wsmatch match;
    if (std::regex_search(value, match, executable_pattern)) {
        return ParsedCommand{match[1].str(), trim_left(match[2].str())};
    }

    const auto first_space = value.find(L' ');
    if (first_space == std::wstring::npos) {
        return ParsedCommand{std::move(value), {}};
    }
    return ParsedCommand{value.substr(0, first_space), trim_left(value.substr(first_space + 1))};
}

std::wstring ParsedCommand::with_executable(const std::wstring& replacement) const {
    const auto executable_value = replacement.find(L' ') == std::wstring::npos
        ? replacement
        : L"\"" + replacement + L"\"";
    return arguments.empty() ? executable_value : executable_value + L" " + arguments;
}

} // namespace simpilot

