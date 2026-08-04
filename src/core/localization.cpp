#include "simpilot/localization.hpp"

#include "simpilot/language_pack.hpp"
#include "simpilot/text_encoding.hpp"
#include "resource.h"

#include <Windows.h>
#include <compressapi.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace simpilot {
namespace {

constexpr std::size_t built_in_language_count = 3;
constexpr std::size_t maximum_language_pack_size = 64U * 1024U * 1024U;
using Catalog = std::unordered_map<std::string, std::wstring>;
using Json = nlohmann::json;

constexpr std::array<std::string_view, 21> ui_keys{
    "ui.app_title", "ui.menu_two", "ui.reload_menu", "ui.settings",
    "ui.open_everything", "ui.everything_unavailable", "ui.repair_everything",
    "ui.repair_everything_success", "ui.repair_everything_failed", "ui.reload_failed",
    "ui.about", "ui.maintenance", "ui.language", "ui.language.english",
    "ui.language.simplified_chinese", "ui.language.traditional_chinese", "ui.exit",
    "ui.configuration_header", "ui.common", "ui.notepad", "ui.calculator",
};

constexpr std::array<std::string_view, 60> settings_keys{
    "settings.title", "settings.heading", "settings.main_menu", "settings.second_menu",
    "settings.open_settings", "settings.open_everything_search", "settings.clear",
    "settings.startup", "settings.menu_theme", "settings.system_theme", "settings.light_theme",
    "settings.dark_theme", "settings.hotkey_hint", "settings.capture",
    "settings.capture_failed", "settings.escape_cancelled", "settings.save", "settings.apply",
    "settings.cancel", "settings.tab.general", "settings.tab.global_hotkeys",
    "settings.tab.windows_shortcuts", "settings.global_hotkeys.heading",
    "settings.global_hotkeys.scope", "settings.global_hotkeys.column.enabled",
    "settings.global_hotkeys.column.hotkey", "settings.global_hotkeys.column.action",
    "settings.global_hotkeys.column.target", "settings.add", "settings.edit", "settings.delete",
    "settings.action.open_application", "settings.action.open_folder", "settings.action.open_file",
    "settings.windows_shortcuts.heading", "settings.windows_shortcuts.scope",
    "settings.windows_shortcuts.runtime", "settings.win_l_unsupported", "settings.tab.menu_icons",
    "settings.menu_icons.heading", "settings.menu_icons.scope", "settings.menu_icons.column.menu",
    "settings.menu_icons.column.name", "settings.menu_icons.column.target",
    "settings.menu_icons.column.source", "settings.menu_icons.select",
    "settings.menu_icons.restore_auto", "settings.menu_icons.automatic",
    "settings.menu_icons.custom", "settings.menu_icons.selection_failed",
    "settings.tab.quick_launch", "settings.quick_launch.heading", "settings.quick_launch.scope",
    "settings.general.scope", "settings.section.startup", "settings.section.appearance",
    "settings.section.built_in_hotkeys", "settings.section.custom_hotkeys",
    "settings.unsaved_changes", "settings.applied",
};

constexpr std::array<std::string_view, 35> custom_hotkey_keys{
    "custom_hotkey.window_title", "custom_hotkey.edit_window_title", "custom_hotkey.title",
    "custom_hotkey.edit_title", "custom_hotkey.trigger_heading", "custom_hotkey.trigger_type",
    "custom_hotkey.allow_modifiers", "custom_hotkey.action_heading",
    "custom_hotkey.open_application", "custom_hotkey.open_folder", "custom_hotkey.open_file",
    "custom_hotkey.program_path", "custom_hotkey.folder_path", "custom_hotkey.file_path",
    "custom_hotkey.arguments", "custom_hotkey.working_directory", "custom_hotkey.browse",
    "custom_hotkey.identity", "custom_hotkey.identity.normal",
    "custom_hotkey.identity.administrator", "custom_hotkey.existing_process",
    "custom_hotkey.existing_process.show_window", "custom_hotkey.existing_process.start_new",
    "custom_hotkey.existing_process.do_nothing", "custom_hotkey.visibility",
    "custom_hotkey.visibility.normal", "custom_hotkey.visibility.minimized",
    "custom_hotkey.visibility.maximized", "custom_hotkey.visibility.hidden", "custom_hotkey.save",
    "custom_hotkey.cancel", "custom_hotkey.modifiers_required", "custom_hotkey.win_l_unsupported",
    "custom_hotkey.capture_failed", "custom_hotkey.invalid_target",
};

constexpr std::array<std::string_view, 20> about_keys{
    "about.positioning", "about.version", "about.tagline", "about.link.home",
    "about.link.releases", "about.link.manual", "about.link.issues",
    "about.product_information", "about.label.system", "about.value.system",
    "about.label.distribution", "about.value.distribution", "about.label.license",
    "about.value.license", "about.local_data", "about.third_party_summary",
    "about.link.license", "about.link.third_party", "about.close", "about.open_failed",
};

static_assert(ui_keys.size() == static_cast<std::size_t>(UiText::calculator) + 1);
static_assert(settings_keys.size() == static_cast<std::size_t>(SettingsText::applied_text) + 1);
static_assert(custom_hotkey_keys.size() ==
              static_cast<std::size_t>(CustomHotKeyText::invalid_target_text) + 1);
static_assert(about_keys.size() == static_cast<std::size_t>(AboutText::open_failed_text) + 1);

std::string canonical_code(const std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

std::optional<Catalog> parse_catalog(const Json& document, std::string* locale,
                                     std::wstring* display_name) noexcept {
    try {
        if (!document.is_object() || !document.contains("locale")
            || !document.at("locale").is_string() || !document.contains("strings")
            || !document.at("strings").is_object()) return std::nullopt;
        const auto value = document.at("locale").get<std::string>();
        if (value.empty()) return std::nullopt;
        Catalog catalog;
        catalog.reserve(document.at("strings").size());
        for (const auto& [key, translated] : document.at("strings").items()) {
            if (key.empty() || !translated.is_string()) return std::nullopt;
            const auto decoded = decode_utf8(translated.get_ref<const std::string&>());
            if (!decoded || decoded->empty() || !catalog.emplace(key, *decoded).second) {
                return std::nullopt;
            }
        }
        if (catalog.empty()) return std::nullopt;
        if (locale) *locale = value;
        if (display_name) {
            *display_name = L"";
            if (document.contains("name") && document.at("name").is_string()) {
                if (const auto decoded = decode_utf8(
                        document.at("name").get_ref<const std::string&>());
                    decoded) {
                    *display_name = *decoded;
                }
            }
        }
        return catalog;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Catalog> load_json_catalog(const std::filesystem::path& directory,
                                         const std::string_view code) noexcept {
    try {
        std::ifstream stream(directory / (std::string(code) + ".json"), std::ios::binary);
        if (!stream) return std::nullopt;
        return parse_catalog(Json::parse(stream), nullptr, nullptr);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::vector<std::byte>> read_file_bytes(
    const std::filesystem::path& path) noexcept {
    try {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) return std::nullopt;
        const auto size = stream.tellg();
        if (size <= 0 || static_cast<std::uintmax_t>(size) > maximum_language_pack_size) {
            return std::nullopt;
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return stream ? std::optional<std::vector<std::byte>>(std::move(bytes)) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::vector<std::byte>> embedded_pack() noexcept {
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_BUILTIN_LANGUAGE_PACK), RT_RCDATA);
    if (!resource) return std::nullopt;
    const auto loaded = LoadResource(module, resource);
    const auto size = SizeofResource(module, resource);
    const auto data = loaded ? LockResource(loaded) : nullptr;
    if (!data || size == 0 || size > maximum_language_pack_size) return std::nullopt;
    const auto* first = static_cast<const std::byte*>(data);
    return std::vector<std::byte>(first, first + size);
}

std::optional<Json> decompress_pack(const std::vector<std::byte>& bytes) noexcept {
    try {
        if (bytes.size() < sizeof(LanguagePackHeader)) return std::nullopt;
        LanguagePackHeader header{};
        std::memcpy(&header, bytes.data(), sizeof(header));
        if (header.magic != language_pack_magic
            || header.version != language_pack_version
            || header.compression != language_pack_xpress_huffman
            || header.uncompressed_size == 0
            || header.uncompressed_size > maximum_language_pack_size
            || header.compressed_size == 0
            || sizeof(header) + header.compressed_size != bytes.size()) return std::nullopt;
        DECOMPRESSOR_HANDLE decompressor = nullptr;
        if (!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &decompressor)) {
            return std::nullopt;
        }
        std::string payload(header.uncompressed_size, '\0');
        SIZE_T written = 0;
        const auto succeeded = Decompress(
            decompressor, bytes.data() + sizeof(header), header.compressed_size,
            payload.data(), payload.size(), &written);
        CloseDecompressor(decompressor);
        if (!succeeded || written != payload.size()) return std::nullopt;
        return Json::parse(payload);
    } catch (...) {
        return std::nullopt;
    }
}

void append_pack(const std::vector<std::byte>& bytes,
                 std::unordered_map<std::string, Catalog>& catalogs,
                 std::unordered_map<std::string, std::wstring>& names,
                 std::unordered_map<std::string, std::string>& codes) noexcept {
    const auto document = decompress_pack(bytes);
    if (!document || !document->is_object() || !document->contains("languages")
        || !document->at("languages").is_array()) return;
    for (const auto& language : document->at("languages")) {
        std::string locale;
        std::wstring name;
        const auto catalog = parse_catalog(language, &locale, &name);
        if (!catalog) continue;
        const auto normalized = canonical_code(locale);
        if (normalized == "en-us" || normalized == "zh-cn" || normalized == "zh-tw") continue;
        if (catalogs.contains(normalized)) continue;
        catalogs.emplace(normalized, *catalog);
        names.emplace(normalized, std::move(name));
        codes.emplace(normalized, std::move(locale));
    }
}

template <typename Enum, std::size_t Size>
std::string_view enum_key(const Enum value,
                          const std::array<std::string_view, Size>& keys) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < keys.size() ? keys[index] : std::string_view{};
}

} // namespace

struct Localization::ResourceBundle {
    std::array<Catalog, built_in_language_count> built_in;
    std::unordered_map<std::string, Catalog> external;
    std::unordered_map<std::string, std::wstring> external_names;
    std::unordered_map<std::string, std::string> external_codes;
};

Localization::Localization(const UiLanguage language,
                           std::filesystem::path resource_directory)
    : language_(language), language_code_(language == UiLanguage::external
          ? std::string(Localization::language_code(UiLanguage::simplified_chinese))
          : std::string(Localization::language_code(language))) {
    const auto use_embedded = resource_directory.empty();
    if (resource_directory.empty()) resource_directory = default_resource_directory();
    std::error_code error;
    const auto absolute = std::filesystem::absolute(resource_directory, error);
    const auto directory = (error ? resource_directory : absolute).lexically_normal();
    static std::mutex cache_mutex;
    static std::unordered_map<std::wstring, std::shared_ptr<const ResourceBundle>> cache;
    const auto cache_key = std::wstring(use_embedded ? L"embedded:" : L"json:") + directory.wstring();
    {
        const std::scoped_lock lock(cache_mutex);
        if (const auto found = cache.find(cache_key); found != cache.end()) {
            resources_ = found->second;
        }
    }
    if (!resources_) {
        auto resources = std::make_shared<ResourceBundle>();
        if (use_embedded) {
            if (const auto pack = embedded_pack()) {
                const auto document = decompress_pack(*pack);
                if (document && document->is_object() && document->contains("languages")
                    && document->at("languages").is_array()) {
                    std::array<Catalog*, built_in_language_count> targets{
                        &resources->built_in[0], &resources->built_in[1], &resources->built_in[2]};
                    for (const auto& language_document : document->at("languages")) {
                        std::string locale;
                        std::wstring ignored;
                        const auto catalog = parse_catalog(language_document, &locale, &ignored);
                        if (!catalog) continue;
                        const auto normalized = canonical_code(locale);
                        const auto index = normalized == "en-us" ? 0
                            : normalized == "zh-cn" ? 1
                            : normalized == "zh-tw" ? 2 : -1;
                        if (index >= 0) *targets[static_cast<std::size_t>(index)] = *catalog;
                    }
                }
            }
        } else {
            resources->built_in[0] = load_json_catalog(directory, "en-US").value_or(Catalog{});
            resources->built_in[1] = load_json_catalog(directory, "zh-CN").value_or(Catalog{});
            resources->built_in[2] = load_json_catalog(directory, "zh-TW").value_or(Catalog{});
        }
        if (const auto external = read_file_bytes(directory / L"Language.lng")) {
            std::unordered_map<std::string, Catalog> all;
            std::unordered_map<std::string, std::wstring> names;
            std::unordered_map<std::string, std::string> codes;
            append_pack(*external, all, names, codes);
            resources->external = std::move(all);
            resources->external_names = std::move(names);
            resources->external_codes = std::move(codes);
        }
        const std::scoped_lock lock(cache_mutex);
        resources_ = cache.try_emplace(cache_key, resources).first->second;
    }
}

Localization::Localization(std::string language_code,
                           std::filesystem::path resource_directory)
    : Localization(language_from_code(language_code), std::move(resource_directory)) {
    set_language(std::move(language_code));
}

UiLanguage Localization::language() const noexcept { return language_; }

std::string_view Localization::language_code() const noexcept { return language_code_; }

void Localization::set_language(const UiLanguage language) noexcept {
    language_ = language;
    language_code_ = std::string(language == UiLanguage::external
        ? Localization::language_code(UiLanguage::simplified_chinese)
        : Localization::language_code(language));
}

void Localization::set_language(std::string language_code) noexcept {
    if (language_code.empty()) language_code = "zh-CN";
    language_ = language_from_code(language_code);
    language_code_ = std::move(language_code);
}

std::wstring_view Localization::text(const UiText value) const noexcept {
    return text(enum_key(value, ui_keys));
}
std::wstring_view Localization::text(const SettingsText value) const noexcept {
    return text(enum_key(value, settings_keys));
}
std::wstring_view Localization::text(const CustomHotKeyText value) const noexcept {
    return text(enum_key(value, custom_hotkey_keys));
}
std::wstring_view Localization::text(const AboutText value) const noexcept {
    return text(enum_key(value, about_keys));
}

std::wstring_view Localization::text(const std::string_view key) const noexcept {
    static constexpr std::wstring_view missing_translation = L"[missing translation]";
    if (key.empty() || !resources_) return missing_translation;
    const auto selected_language = canonical_code(language_code_);
    if (selected_language == "en-us") {
        const auto found = resources_->built_in[0].find(std::string(key));
        if (found != resources_->built_in[0].end()) return found->second;
    } else if (selected_language == "zh-cn") {
        const auto found = resources_->built_in[1].find(std::string(key));
        if (found != resources_->built_in[1].end()) return found->second;
    } else if (selected_language == "zh-tw") {
        const auto found = resources_->built_in[2].find(std::string(key));
        if (found != resources_->built_in[2].end()) return found->second;
    } else if (const auto external = resources_->external.find(selected_language);
               external != resources_->external.end()) {
        const auto found = external->second.find(std::string(key));
        if (found != external->second.end()) return found->second;
    }
    const auto fallback = resources_->built_in[0].find(std::string(key));
    return fallback == resources_->built_in[0].end() ? missing_translation : fallback->second;
}

std::vector<LanguageInfo> Localization::available_languages() const {
    std::vector<LanguageInfo> result;
    result.reserve(built_in_language_count + resources_->external.size());
    for (const auto code : {std::string("zh-CN"), std::string("zh-TW"), std::string("en-US")}) {
        result.push_back(LanguageInfo{code, std::wstring(language_display_name(code)), true});
    }
    for (const auto& [normalized, code] : resources_->external_codes) {
        const auto found = resources_->external_names.find(normalized);
        result.push_back(LanguageInfo{code,
            found != resources_->external_names.end() && !found->second.empty()
                ? found->second : std::wstring(decode_utf8(code).value_or(L"")), false});
    }
    return result;
}

std::wstring_view Localization::language_display_name(
    const std::string_view code) const noexcept {
    const auto normalized = canonical_code(code);
    if (normalized == "en-us") return text(UiText::english);
    if (normalized == "zh-cn") return text(UiText::simplified_chinese);
    if (normalized == "zh-tw") return text(UiText::traditional_chinese);
    const auto found = resources_->external_names.find(normalized);
    if (found != resources_->external_names.end() && !found->second.empty()) return found->second;
    static const std::wstring empty;
    return empty;
}

std::string_view Localization::language_code(const UiLanguage language) noexcept {
    switch (language) {
    case UiLanguage::english: return "en-US";
    case UiLanguage::simplified_chinese: return "zh-CN";
    case UiLanguage::traditional_chinese: return "zh-TW";
    case UiLanguage::external: return "";
    }
    return "zh-CN";
}

UiLanguage Localization::language_from_code(const std::string_view code) noexcept {
    const auto normalized = canonical_code(code);
    if (normalized == "en-us") return UiLanguage::english;
    if (normalized == "zh-tw") return UiLanguage::traditional_chinese;
    if (normalized == "zh-cn") return UiLanguage::simplified_chinese;
    return UiLanguage::external;
}

std::filesystem::path Localization::default_resource_directory() {
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return std::filesystem::current_path();
    executable.resize(length);
    return std::filesystem::path(executable).parent_path();
}

} // namespace simpilot
