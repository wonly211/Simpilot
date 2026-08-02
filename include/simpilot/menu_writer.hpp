#pragma once

#include "simpilot/menu_model.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace simpilot {

class MenuWriter final {
public:
    [[nodiscard]] static std::optional<std::wstring> validate(
        const MenuDocument& document);
    [[nodiscard]] static std::wstring serialize(const MenuDocument& document);
    static void save_file(const std::filesystem::path& path,
                          const MenuDocument& document);
};

} // namespace simpilot
