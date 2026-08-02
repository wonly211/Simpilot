#pragma once

#include <filesystem>
#include <string>

namespace simpilot {

[[nodiscard]] std::wstring read_configuration_text(const std::filesystem::path& path);
void write_configuration_text(const std::filesystem::path& path, const std::wstring& text);

} // namespace simpilot
