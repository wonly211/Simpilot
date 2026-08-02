#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace simpilot {

[[nodiscard]] std::string encode_utf8(std::wstring_view value);
[[nodiscard]] std::optional<std::wstring> decode_utf8(std::string_view value) noexcept;

} // namespace simpilot
