#pragma once

#include "simpilot/hotkey.hpp"
#include "simpilot/localization.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace simpilot {

struct AppSettings {
    UiLanguage language = UiLanguage::simplified_chinese;
    // Empty for the three built-in languages; populated with the locale code
    // when an external Language.lng language is selected.
    std::string language_code;
    bool start_with_windows = false;
    BuiltInHotKey open_settings{};

    bool operator==(const AppSettings&) const = default;
};

class SettingsDocument;

class AppSettingsStore final {
public:
    using DiagnosticSink = std::function<void(std::wstring_view)>;

    [[nodiscard]] static AppSettings load(
        const std::filesystem::path& path,
        DiagnosticSink diagnostic_sink = {}) noexcept;
    static void write(SettingsDocument& document, const AppSettings& settings);
    [[nodiscard]] static bool save(const std::filesystem::path& path,
                                   const AppSettings& settings,
                                   const SettingsDocument* base_document = nullptr) noexcept;
};

class StartupRegistration final {
public:
    [[nodiscard]] static bool apply(bool enabled,
                                    const std::filesystem::path& executable_path) noexcept;
};

} // namespace simpilot
