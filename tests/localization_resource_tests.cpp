#include <Windows.h>

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using KeySet = std::set<std::string>;

struct CatalogSummary {
    KeySet keys;
    std::map<std::string, std::size_t> placeholder_counts;
};

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path executable_directory() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    require(length > 0 && length < path.size(), "Resolve test executable directory");
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

std::size_t placeholder_count(const std::string& value) {
    std::size_t count = 0;
    for (std::size_t index = 0; index + 1 < value.size(); ++index) {
        if (value[index] == '{' && value[index + 1] == '}') {
            ++count;
            ++index;
        }
    }
    return count;
}

CatalogSummary load_catalog(const std::filesystem::path& path,
                            const std::string& expected_locale) {
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "Open packaged language resource");
    const auto document = nlohmann::json::parse(stream);
    require(document.is_object(), "Language resource root is an object");
    require(document.value("locale", std::string{}) == expected_locale,
            "Language resource locale matches its file name");
    require(document.contains("strings") && document.at("strings").is_object(),
            "Language resource contains a strings object");

    CatalogSummary summary;
    for (const auto& [key, value] : document.at("strings").items()) {
        require(!key.empty(), "Language resource key is not empty");
        require(value.is_string(), "Language resource value is a string");
        const auto& translated = value.get_ref<const std::string&>();
        require(!translated.empty(), "Language resource value is not empty");
        summary.keys.insert(key);
        summary.placeholder_counts.emplace(key, placeholder_count(translated));
    }
    return summary;
}

} // namespace

int wmain() {
    try {
        const auto language_directory = executable_directory() / L"Languages";
        const std::array locales{"en-US", "zh-CN", "zh-TW"};
        const auto english = load_catalog(
            language_directory / L"en-US.json", locales[0]);
        require(!english.keys.empty(), "English language resource is not empty");

        for (std::size_t index = 1; index < locales.size(); ++index) {
            const auto locale = std::string(locales[index]);
            const auto catalog = load_catalog(
                language_directory / (locale + ".json"), locale);
            require(catalog.keys == english.keys,
                    "Every built-in language has the canonical English key set");
            require(catalog.placeholder_counts == english.placeholder_counts,
                    "Format placeholders match across built-in languages");
        }
        std::wcout << L"All Simpilot language resources are complete.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Localization resource test failure: " << error.what() << '\n';
        return 1;
    }
}
