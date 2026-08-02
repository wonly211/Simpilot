#pragma once

#include "simpilot/menu_model.hpp"

#include <Windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace simpilot {

class MenuIconCache final {
public:
    explicit MenuIconCache(std::filesystem::path cache_directory);
    ~MenuIconCache();

    MenuIconCache(const MenuIconCache&) = delete;
    MenuIconCache& operator=(const MenuIconCache&) = delete;

    [[nodiscard]] HICON icon_for(const MenuEntry& entry);
    [[nodiscard]] HICON icon_for_customization(const std::wstring& custom_key,
                                               const std::wstring& icon_source,
                                               MenuEntryKind kind);
    [[nodiscard]] HICON folder_icon();
    [[nodiscard]] bool has_custom_icon(const std::wstring& custom_key) const noexcept;
    [[nodiscard]] bool set_custom_icon(const std::wstring& custom_key,
                                       const std::filesystem::path& source,
                                       int source_index);
    [[nodiscard]] bool remove_custom_icon(const std::wstring& custom_key) noexcept;
    void clear() noexcept;

    [[nodiscard]] static std::wstring custom_key_for(const MenuEntry& entry);
    [[nodiscard]] static std::optional<std::wstring> target_for(
        const MenuEntry& entry);

private:
    [[nodiscard]] HICON load_icon(const std::wstring& memory_key,
                                  const std::wstring& cache_key,
                                  const std::wstring& shell_path,
                                  DWORD attributes,
                                  bool use_file_attributes);
    [[nodiscard]] std::filesystem::path cache_path_for(
        const std::wstring& key) const;
    [[nodiscard]] std::filesystem::path custom_icon_path_for(
        const std::wstring& key) const;

    std::filesystem::path cache_directory_;
    std::unordered_map<std::wstring, HICON> icons_;
};

} // namespace simpilot
