#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace simpilot {

// Retains unknown lines, comments, sections and each line's original terminator.
// Legacy Setting.ini lookup is deliberately case-insensitive and sectionless.
class SettingsDocument final {
public:
    [[nodiscard]] static SettingsDocument parse(std::string_view content);
    [[nodiscard]] static SettingsDocument load(const std::filesystem::path& path);
    [[nodiscard]] std::optional<std::wstring> get(std::wstring_view key) const;
    void set(std::wstring_view section, std::wstring_view key, std::wstring_view value);
    void erase(std::wstring_view key);
    void erase_numbered(std::wstring_view prefix,
                        std::span<const std::wstring_view> suffixes);
    void overlay(const SettingsDocument& updates);
    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] bool save(const std::filesystem::path& path) const noexcept;

private:
    struct Line {
        std::wstring text;
        std::wstring ending;
    };
    std::vector<Line> lines_;
    bool bom_ = false;
};

} // namespace simpilot
