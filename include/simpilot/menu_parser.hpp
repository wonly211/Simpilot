#pragma once

#include "simpilot/menu_model.hpp"

#include <filesystem>
#include <string>

namespace simpilot {

class MenuParser final {
public:
    [[nodiscard]] static MenuDocument parse_file(const std::filesystem::path& path);
    [[nodiscard]] static MenuDocument parse(const std::wstring& content);
};

} // namespace simpilot
