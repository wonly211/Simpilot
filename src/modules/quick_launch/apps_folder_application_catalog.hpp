#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace simpilot {

enum class AppsFolderApplicationScope {
    packaged,
    all,
};

struct AppsFolderApplication {
    std::wstring display_name;
    std::wstring package_family_name;
    std::wstring aumid;

    bool operator==(const AppsFolderApplication&) const = default;
};

class AppsFolderApplicationCatalog final {
public:
    [[nodiscard]] static bool is_launchable(
        const AppsFolderApplication& application) noexcept;
    [[nodiscard]] static bool is_packaged(
        const AppsFolderApplication& application) noexcept;
    [[nodiscard]] static std::vector<AppsFolderApplication> normalize(
        std::vector<AppsFolderApplication> applications,
        AppsFolderApplicationScope scope);
    [[nodiscard]] static std::vector<AppsFolderApplication> enumerate(
        AppsFolderApplicationScope scope);
    [[nodiscard]] static std::wstring shell_target(std::wstring_view aumid);
};

} // namespace simpilot
