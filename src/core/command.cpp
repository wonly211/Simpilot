#include "simpilot/command.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
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

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
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

bool is_terminal_executable(const std::wstring_view executable) noexcept {
    try {
        const auto filename = lowercase(
            std::filesystem::path(executable).filename().wstring());
        static constexpr std::array<std::wstring_view, 9> terminal_names{
            L"cmd", L"cmd.exe",
            L"powershell", L"powershell.exe",
            L"pwsh", L"pwsh.exe",
            L"wt", L"wt.exe",
            L"windowsterminal.exe",
        };
        return std::ranges::find(terminal_names, filename) != terminal_names.end();
    } catch (...) {
        return false;
    }
}

} // namespace simpilot
