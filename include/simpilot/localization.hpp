#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <string_view>

namespace simpilot {

enum class UiLanguage {
    english,
    simplified_chinese,
    traditional_chinese,
    external,
};

struct LanguageInfo {
    std::string code;
    std::wstring display_name;
    bool built_in = false;
};







enum class AboutText {
    positioning_text,
    version_text,
    tagline_text,
    home_link_text,
    releases_link_text,
    manual_link_text,
    issues_link_text,
    product_information_text,
    system_label_text,
    system_value_text,
    distribution_label_text,
    distribution_value_text,
    license_label_text,
    license_value_text,
    local_data_text,
    third_party_summary_text,
    license_link_text,
    third_party_link_text,
    close_text,
    open_failed_text,
};

class Localization final {
public:
    explicit Localization(UiLanguage language,
                          std::filesystem::path resource_directory = {});
    explicit Localization(std::string language_code,
                          std::filesystem::path resource_directory = {});

    [[nodiscard]] UiLanguage language() const noexcept;
    [[nodiscard]] std::string_view language_code() const noexcept;
    void set_language(UiLanguage language) noexcept;
    void set_language(std::string language_code) noexcept;
    [[nodiscard]] std::wstring_view text(AboutText text) const noexcept;
    [[nodiscard]] std::wstring_view text(std::string_view key) const noexcept;
    [[nodiscard]] std::vector<LanguageInfo> available_languages() const;
    [[nodiscard]] std::wstring_view language_display_name(
        std::string_view language_code) const noexcept;
    [[nodiscard]] static std::string_view language_code(UiLanguage language) noexcept;
    [[nodiscard]] static UiLanguage language_from_code(
        std::string_view language_code) noexcept;
    [[nodiscard]] static std::filesystem::path default_resource_directory();

private:
    struct ResourceBundle;

    UiLanguage language_;
    std::string language_code_;
    std::shared_ptr<const ResourceBundle> resources_;
};

} // namespace simpilot
