#include "simpilot/variable_expander.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwctype>

namespace simpilot {
namespace {

std::wstring uppercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towupper);
    return value;
}

std::optional<std::wstring> environment_variable(const std::wstring& name) {
    const auto required = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }
    std::wstring result(required, L'\0');
    const auto written = GetEnvironmentVariableW(name.c_str(), result.data(), required);
    result.resize(written);
    return result;
}

} // namespace

VariableExpander::VariableExpander(std::wstring config_directory)
    : config_directory_(std::move(config_directory)) {}

const std::wstring& VariableExpander::config_directory() const noexcept {
    return config_directory_;
}

std::wstring VariableExpander::expand(const std::wstring& input) const {
    std::wstring result;
    result.reserve(input.size());

    for (std::size_t index = 0; index < input.size();) {
        if (input[index] != L'%') {
            result.push_back(input[index++]);
            continue;
        }

        const auto closing = input.find(L'%', index + 1);
        if (closing != std::wstring::npos && closing > index + 1) {
            const auto name = input.substr(index + 1, closing - index - 1);
            const auto token = input.substr(index, closing - index + 1);
            if (const auto value = resolve_variable(name)) {
                result += *value;
            } else {
                result += token;
            }
            index = closing + 1;
            continue;
        }
        result.push_back(input[index++]);
    }
    return result;
}

std::optional<std::wstring> VariableExpander::resolve_variable(const std::wstring& name) const {
    const auto normalized = uppercase(name);
    if (normalized == L"SIMPILOTCONFIGDIR") return config_directory_;
    return environment_variable(name);
}

} // namespace simpilot
