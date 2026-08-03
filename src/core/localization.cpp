#include "simpilot/localization.hpp"

#include "simpilot/text_encoding.hpp"

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>

namespace simpilot {
namespace {

constexpr auto language_file_name = L"language.txt";
constexpr auto language_directory_name = L"Languages";

using Catalog = std::unordered_map<std::string, std::wstring>;

constexpr std::array<std::string_view, 21> ui_keys{
    "ui.app_title",
    "ui.menu_two",
    "ui.reload_menu",
    "ui.settings",
    "ui.open_everything",
    "ui.everything_unavailable",
    "ui.repair_everything",
    "ui.repair_everything_success",
    "ui.repair_everything_failed",
    "ui.reload_failed",
    "ui.about",
    "ui.maintenance",
    "ui.language",
    "ui.language.english",
    "ui.language.simplified_chinese",
    "ui.language.traditional_chinese",
    "ui.exit",
    "ui.configuration_header",
    "ui.common",
    "ui.notepad",
    "ui.calculator",
};

constexpr std::array<std::string_view, 60> settings_keys{
    "settings.title", "settings.heading", "settings.main_menu",
    "settings.second_menu", "settings.open_settings",
    "settings.open_everything_search", "settings.clear", "settings.startup",
    "settings.menu_theme", "settings.system_theme", "settings.light_theme",
    "settings.dark_theme", "settings.hotkey_hint", "settings.capture",
    "settings.capture_failed", "settings.escape_cancelled", "settings.save",
    "settings.apply", "settings.cancel", "settings.tab.general",
    "settings.tab.global_hotkeys", "settings.tab.windows_shortcuts",
    "settings.global_hotkeys.heading", "settings.global_hotkeys.scope",
    "settings.global_hotkeys.column.enabled", "settings.global_hotkeys.column.hotkey",
    "settings.global_hotkeys.column.action", "settings.global_hotkeys.column.target",
    "settings.add", "settings.edit", "settings.delete",
    "settings.action.open_application", "settings.action.open_folder",
    "settings.action.open_file", "settings.windows_shortcuts.heading",
    "settings.windows_shortcuts.scope", "settings.windows_shortcuts.runtime",
    "settings.win_l_unsupported", "settings.tab.menu_icons",
    "settings.menu_icons.heading", "settings.menu_icons.scope",
    "settings.menu_icons.column.menu", "settings.menu_icons.column.name",
    "settings.menu_icons.column.target", "settings.menu_icons.column.source",
    "settings.menu_icons.select", "settings.menu_icons.restore_auto",
    "settings.menu_icons.automatic", "settings.menu_icons.custom",
    "settings.menu_icons.selection_failed", "settings.tab.quick_launch",
    "settings.quick_launch.heading", "settings.quick_launch.scope",
    "settings.general.scope", "settings.section.startup",
    "settings.section.appearance", "settings.section.built_in_hotkeys",
    "settings.section.custom_hotkeys", "settings.unsaved_changes",
    "settings.applied",
};

constexpr std::array<std::string_view, 35> custom_hotkey_keys{
    "custom_hotkey.window_title", "custom_hotkey.edit_window_title",
    "custom_hotkey.title", "custom_hotkey.edit_title",
    "custom_hotkey.trigger_heading", "custom_hotkey.trigger_type",
    "custom_hotkey.allow_modifiers", "custom_hotkey.action_heading",
    "custom_hotkey.open_application", "custom_hotkey.open_folder",
    "custom_hotkey.open_file", "custom_hotkey.program_path",
    "custom_hotkey.folder_path", "custom_hotkey.file_path",
    "custom_hotkey.arguments", "custom_hotkey.working_directory",
    "custom_hotkey.browse", "custom_hotkey.identity",
    "custom_hotkey.identity.normal", "custom_hotkey.identity.administrator",
    "custom_hotkey.existing_process", "custom_hotkey.existing_process.show_window",
    "custom_hotkey.existing_process.start_new", "custom_hotkey.existing_process.do_nothing",
    "custom_hotkey.visibility", "custom_hotkey.visibility.normal",
    "custom_hotkey.visibility.minimized", "custom_hotkey.visibility.maximized",
    "custom_hotkey.visibility.hidden", "custom_hotkey.save", "custom_hotkey.cancel",
    "custom_hotkey.modifiers_required", "custom_hotkey.win_l_unsupported",
    "custom_hotkey.capture_failed", "custom_hotkey.invalid_target",
};

constexpr std::array<std::string_view, 20> about_keys{
    "about.positioning", "about.version", "about.tagline",
    "about.link.home", "about.link.releases", "about.link.manual",
    "about.link.issues", "about.product_information", "about.label.system",
    "about.value.system", "about.label.distribution", "about.value.distribution",
    "about.label.license", "about.value.license", "about.local_data",
    "about.third_party_summary", "about.link.license", "about.link.third_party",
    "about.close", "about.open_failed",
};

static_assert(ui_keys.size() == static_cast<std::size_t>(UiText::calculator) + 1);
static_assert(settings_keys.size() ==
              static_cast<std::size_t>(SettingsText::applied_text) + 1);
static_assert(custom_hotkey_keys.size() ==
              static_cast<std::size_t>(CustomHotKeyText::invalid_target_text) + 1);
static_assert(about_keys.size() ==
              static_cast<std::size_t>(AboutText::open_failed_text) + 1);

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](const unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](const unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::size_t language_index(const UiLanguage language) noexcept {
    switch (language) {
    case UiLanguage::english: return 0;
    case UiLanguage::simplified_chinese: return 1;
    case UiLanguage::traditional_chinese: return 2;
    }
    return 0;
}

Catalog load_catalog(const std::filesystem::path& resource_directory,
                     const UiLanguage language) noexcept {
    try {
        const auto code = Localization::language_code(language);
        const auto path = resource_directory / (std::string(code) + ".json");
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return {};

        const auto document = nlohmann::json::parse(stream);
        if (!document.is_object()
            || document.value("locale", std::string{}) != code
            || !document.contains("strings")
            || !document.at("strings").is_object()) {
            return {};
        }

        Catalog catalog;
        const auto& strings = document.at("strings");
        catalog.reserve(strings.size());
        for (auto item = strings.begin(); item != strings.end(); ++item) {
            if (!item.value().is_string()) return {};
            const auto decoded = decode_utf8(item.value().get_ref<const std::string&>());
            if (!decoded) return {};
            const auto [ignored, inserted] = catalog.emplace(item.key(), *decoded);
            if (!inserted) return {};
        }
        return catalog;
    } catch (...) {
        return {};
    }
}

std::filesystem::path normalized_resource_directory(std::filesystem::path directory) {
    if (directory.empty()) directory = Localization::default_resource_directory();
    std::error_code error;
    const auto absolute = std::filesystem::absolute(directory, error);
    return (error ? directory : absolute).lexically_normal();
}

template <typename Enum, std::size_t Size>
std::string_view enum_key(const Enum value,
                          const std::array<std::string_view, Size>& keys) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < keys.size() ? keys[index] : std::string_view{};
}

} // namespace

struct Localization::ResourceBundle {
    std::array<Catalog, 3> catalogs;
};

Localization::Localization(const UiLanguage language,
                           std::filesystem::path resource_directory)
    : language_(language) {
    const auto directory = normalized_resource_directory(std::move(resource_directory));

    // Keep catalogs alive for the process lifetime. Besides avoiding repeated parsing, this
    // guarantees that views returned by short-lived UI helper objects remain valid.
    static std::mutex cache_mutex;
    static std::unordered_map<std::wstring, std::shared_ptr<const ResourceBundle>> cache;
    const auto cache_key = directory.wstring();
    {
        const std::scoped_lock lock(cache_mutex);
        if (const auto found = cache.find(cache_key); found != cache.end()) {
            resources_ = found->second;
        }
    }
    if (resources_) return;

    auto resources = std::make_shared<ResourceBundle>();
    resources->catalogs[0] = load_catalog(directory, UiLanguage::english);
    resources->catalogs[1] = load_catalog(directory, UiLanguage::simplified_chinese);
    resources->catalogs[2] = load_catalog(directory, UiLanguage::traditional_chinese);
    const std::scoped_lock lock(cache_mutex);
    resources_ = cache.try_emplace(cache_key, resources).first->second;
}

Localization Localization::load(const std::filesystem::path& config_directory,
                                std::filesystem::path resource_directory) {
    std::ifstream stream(config_directory / language_file_name, std::ios::binary);
    if (stream) {
        std::string value((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        value = trim(std::move(value));
        if (value == "en-US") {
            return Localization(UiLanguage::english, std::move(resource_directory));
        }
        if (value == "zh-CN") {
            return Localization(UiLanguage::simplified_chinese, std::move(resource_directory));
        }
        if (value == "zh-TW") {
            return Localization(UiLanguage::traditional_chinese, std::move(resource_directory));
        }
    }
    return Localization(UiLanguage::simplified_chinese, std::move(resource_directory));
}

bool Localization::save(const std::filesystem::path& config_directory) const noexcept {
    try {
        std::filesystem::create_directories(config_directory);
        const auto path = config_directory / language_file_name;
        const auto temporary_path = path.wstring() + L".tmp";
        {
            std::ofstream stream(temporary_path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            const auto code = language_code(language_);
            stream.write(code.data(), static_cast<std::streamsize>(code.size()));
            stream.put('\n');
            if (!stream) return false;
        }
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::rename(temporary_path, path);
        return true;
    } catch (...) {
        return false;
    }
}

UiLanguage Localization::language() const noexcept {
    return language_;
}

void Localization::set_language(const UiLanguage language) noexcept {
    language_ = language;
}

std::wstring_view Localization::text(const UiText text_value) const noexcept {
    return text(enum_key(text_value, ui_keys));
}

std::wstring_view Localization::text(const SettingsText text_value) const noexcept {
    return text(enum_key(text_value, settings_keys));
}

std::wstring_view Localization::text(const CustomHotKeyText text_value) const noexcept {
    return text(enum_key(text_value, custom_hotkey_keys));
}

std::wstring_view Localization::text(const AboutText text_value) const noexcept {
    return text(enum_key(text_value, about_keys));
}

std::wstring_view Localization::text(const std::string_view key) const noexcept {
    static constexpr std::wstring_view missing_translation = L"[missing translation]";
    if (key.empty() || !resources_) return missing_translation;

    const auto find = [&](const UiLanguage language) -> const std::wstring* {
        const auto& catalog = resources_->catalogs[language_index(language)];
        const auto found = catalog.find(std::string(key));
        return found == catalog.end() ? nullptr : &found->second;
    };
    if (const auto* value = find(language_)) return *value;
    if (language_ != UiLanguage::english) {
        if (const auto* value = find(UiLanguage::english)) return *value;
    }
    return missing_translation;
}

std::string_view Localization::language_code(const UiLanguage language) noexcept {
    switch (language) {
    case UiLanguage::english: return "en-US";
    case UiLanguage::simplified_chinese: return "zh-CN";
    case UiLanguage::traditional_chinese: return "zh-TW";
    }
    return "en-US";
}

std::filesystem::path Localization::default_resource_directory() {
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        return std::filesystem::current_path() / language_directory_name;
    }
    executable.resize(length);
    return std::filesystem::path(executable).parent_path() / language_directory_name;
}

} // namespace simpilot
