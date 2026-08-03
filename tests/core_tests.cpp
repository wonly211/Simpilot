#include "simpilot/app_settings.hpp"
#include "simpilot/command.hpp"
#include "simpilot/config_file.hpp"
#include "simpilot/config_watcher.hpp"
#include "simpilot/everything.hpp"
#include "simpilot/localization.hpp"
#include "simpilot/logger.hpp"
#include "simpilot/hotkey.hpp"
#include "simpilot/menu_parser.hpp"
#include "simpilot/menu_writer.hpp"
#include "simpilot/program_resolver.hpp"
#include "simpilot/program_cache.hpp"
#include "simpilot/variable_expander.hpp"

#include <Windows.h>

#include <array>
#include <filesystem>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
void require_equal(const T& actual, const T& expected, const std::string& message) {
    require(actual == expected, message);
}

const simpilot::MenuCategory& category(const std::unique_ptr<simpilot::MenuElement>& element) {
    const auto* value = dynamic_cast<const simpilot::MenuCategory*>(element.get());
    require(value != nullptr, "Expected category");
    return *value;
}

const simpilot::MenuEntry& entry(const std::unique_ptr<simpilot::MenuElement>& element) {
    const auto* value = dynamic_cast<const simpilot::MenuEntry*>(element.get());
    require(value != nullptr, "Expected entry");
    return *value;
}

class FakeProgramSearch final : public simpilot::IProgramSearch {
public:
    bool available_value = true;
    std::vector<simpilot::ProgramCandidate> candidates;

    [[nodiscard]] bool available() const override { return available_value; }
    [[nodiscard]] std::vector<simpilot::ProgramCandidate> find_exact_file_name(
        const std::wstring&) const override {
        return candidates;
    }
};

void parser_preserves_hierarchy() {
    const auto document = simpilot::MenuParser::parse(
        L"; comment\n-Common(&A)\nChrome|chrome.exe\n--Office\nWord|winword.exe\n--\n"
        L"Calculator|calc.exe\n-\nRoot|cmd.exe\n");
    const auto& common = category(document.root->children.at(0));
    require_equal(common.name, std::wstring(L"Common"), "Category name");
    require(common.access_key && *common.access_key == L'A',
            "Parse a category access key");
    const auto& chrome = entry(common.children.at(0));
    require_equal(chrome.display_name, std::wstring(L"Chrome"), "Entry display name");
    require_equal(chrome.value, std::wstring(L"chrome.exe"), "Entry command");
    const auto& office = category(common.children.at(1));
    require_equal(entry(office.children.at(0)).value, std::wstring(L"winword.exe"), "Nested command");
    require(dynamic_cast<const simpilot::MenuSeparator*>(common.children.at(2).get()) != nullptr,
            "Nested reset separator");
    require_equal(entry(common.children.at(3)).value, std::wstring(L"calc.exe"), "Reset command");
    require(dynamic_cast<const simpilot::MenuSeparator*>(document.root->children.at(1).get()) != nullptr,
            "Root reset separator");
}

void parser_recognizes_entry_kinds_and_admin_marker() {
    const auto document = simpilot::MenuParser::parse(
        L"-Input\nFind|notepad.exe\n"
        L"Search|https://example.test/\nTerminal (Admin)[#]|cmd.exe\n"
        L"powershell.exe(&P)[#]\n");
    const auto& input = category(document.root->children.at(0));
    const auto& find = entry(input.children.at(0));
    require(find.kind == simpilot::MenuEntryKind::command, "Command kind");
    require(entry(input.children.at(1)).kind == simpilot::MenuEntryKind::web, "Web kind");
    const auto& terminal = entry(input.children.at(2));
    require(terminal.run_as_administrator && terminal.display_name == L"Terminal (Admin)", "Admin marker");
    const auto& direct = entry(input.children.at(3));
    require(direct.run_as_administrator && direct.display_name == L"powershell"
                && direct.access_key && *direct.access_key == L'P',
            "Direct command admin marker and access key");
}

void menu_access_keys_round_trip() {
    const auto original = simpilot::MenuParser::parse(
        L"-\u5e38\u7528(&A)\n\u8bb0\u4e8b\u672c(&N)|notepad.exe\n\u7ba1\u7406\u5458(&T)[#]|cmd.exe\n");
    const auto& category_value = category(original.root->children.at(0));
    require(category_value.access_key && *category_value.access_key == L'A',
            "Parse the category access key");
    const auto& normal = entry(category_value.children.at(0));
    require(normal.display_name == L"\u8bb0\u4e8b\u672c"
                && normal.access_key && *normal.access_key == L'N',
            "Parse a launch item access key");
    const auto& administrator = entry(category_value.children.at(1));
    require(administrator.run_as_administrator && administrator.access_key
                && *administrator.access_key == L'T',
            "Parse an administrator access key");

    const auto serialized = simpilot::MenuWriter::serialize(original);
    require(serialized.find(L"-\u5e38\u7528(&A)\r\n") != std::wstring::npos,
            "Serialize the category access key");
    require(serialized.find(L"\u7ba1\u7406\u5458(&T)[#]|cmd.exe\r\n") != std::wstring::npos,
            "Serialize the administrator access key before its marker");
    const auto restored = simpilot::MenuParser::parse(serialized);
    require(category(restored.root->children.at(0)).access_key == std::optional<wchar_t>{L'A'},
            "Round-trip the category access key");
}

void menu_writer_round_trips_hierarchy_and_utf8() {
    const auto original = simpilot::MenuParser::parse(
        L"-\u5e38\u7528\n\u7f16\u8f91\u5668|notepad.exe\n--Office\nWord[#]|winword.exe --safe\n"
        L"--\n\u641c\u7d22|https://example.test/\n-\nRoot|cmd.exe\n");
    require(!simpilot::MenuWriter::validate(original), "Validate a parsed menu document");
    const auto serialized = simpilot::MenuWriter::serialize(original);
    const auto restored = simpilot::MenuParser::parse(serialized);
    const auto& common = category(restored.root->children.at(0));
    require_equal(common.name, std::wstring(L"\u5e38\u7528"),
                  "Round-trip a UTF-8 category name");
    require_equal(entry(common.children.at(0)).value, std::wstring(L"notepad.exe"),
                  "Round-trip a command item");
    const auto& office = category(common.children.at(1));
    require(entry(office.children.at(0)).run_as_administrator,
            "Round-trip the administrator marker");
    require(dynamic_cast<const simpilot::MenuSeparator*>(common.children.at(2).get()),
            "Round-trip a parent separator after a nested category");
    require(entry(common.children.at(3)).kind == simpilot::MenuEntryKind::web,
            "Round-trip a web item");
    require(dynamic_cast<const simpilot::MenuSeparator*>(restored.root->children.at(1).get()),
            "Round-trip a root separator");

    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-menu-writer-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"Simpilot.ini";
    simpilot::MenuWriter::save_file(path, restored);
    const auto from_disk = simpilot::MenuParser::parse_file(path);
    require_equal(category(from_disk.root->children.at(0)).name,
                  std::wstring(L"\u5e38\u7528"), "Write and read the menu as UTF-8");
    require(!std::filesystem::exists(path.wstring() + L".tmp"),
            "Remove the temporary file after atomic replacement");
    std::filesystem::remove_all(root);
}

void menu_writer_rejects_reserved_admin_suffix_for_normal_items() {
    simpilot::MenuDocument document;
    document.root = std::make_unique<simpilot::MenuCategory>(L"");
    document.root->children.push_back(std::make_unique<simpilot::MenuEntry>(
        L"Normal item[#]", L"notepad.exe", simpilot::MenuEntryKind::command, 1, false));
    require(simpilot::MenuWriter::validate(document).has_value(),
            "Reject the administrator suffix on a normal menu item");

    document.root->children.clear();
    document.root->children.push_back(std::make_unique<simpilot::MenuCategory>(L"   "));
    require(simpilot::MenuWriter::validate(document).has_value(),
            "Reject an all-whitespace category name");

    document.root->children.clear();
    auto invalid_access_key = std::make_unique<simpilot::MenuEntry>(
        L"Invalid access key", L"notepad.exe", simpilot::MenuEntryKind::command, 1);
    invalid_access_key->access_key = L'&';
    document.root->children.push_back(std::move(invalid_access_key));
    require(simpilot::MenuWriter::validate(document).has_value(),
            "Reject an invalid menu access key");
}

void parser_rejects_non_utf8_configuration() {
    const auto file = std::filesystem::temp_directory_path()
        / L"simpilot-invalid-encoding-test.ini";
    {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        const char invalid_utf8[] = {'\xFF', '\xFE', 'A', '\0'};
        stream.write(invalid_utf8, sizeof(invalid_utf8));
    }
    auto rejected = false;
    try {
        (void)simpilot::MenuParser::parse_file(file);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    std::filesystem::remove(file);
    require(rejected, "Reject menu configuration that is not UTF-8");
}

void variables_expand_config_and_windows_environment() {
    const simpilot::VariableExpander expander(L"D:\\Config");
    const auto expanded = expander.expand(
        L"%SimpilotConfigDir%\\app.exe %DOES_NOT_EXIST_SIMPILOT%");
    require(expanded.starts_with(L"D:\\Config\\app.exe "), "Config directory expansion");
    require(expanded.ends_with(L"%DOES_NOT_EXIST_SIMPILOT%"), "Unknown variable preservation");
    const auto appdata = expander.expand(L"%APPDATA%");
    require(!appdata.empty() && appdata != L"%APPDATA%", "Standard AppData expansion");
}

void commands_parse_and_replace_executables() {
    const auto parsed = simpilot::ParsedCommand::try_parse(L"\"C:\\Program Files\\Tool\\tool.exe\" --flag");
    require(parsed && parsed->executable == L"C:\\Program Files\\Tool\\tool.exe" && parsed->arguments == L"--flag",
            "Quoted command parsing");
    require_equal(parsed->with_executable(L"D:\\New Tool\\tool.exe"),
                  std::wstring(L"\"D:\\New Tool\\tool.exe\" --flag"), "Executable replacement");
}

void resolver_falls_back_to_everything_search() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-everything-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto older_directory = root / L"older";
    const auto newer_directory = root / L"newer";
    std::filesystem::create_directories(older_directory);
    std::filesystem::create_directories(newer_directory);
    const auto older = older_directory / L"simpilot-search-only.exe";
    const auto newer = newer_directory / L"simpilot-search-only.exe";
    std::ofstream(older).put('\0');
    std::ofstream(newer).put('\0');

    FakeProgramSearch search;
    search.candidates = {
        {older, 1, std::filesystem::last_write_time(older)},
        {newer, 2, std::filesystem::last_write_time(newer)},
    };
    const simpilot::ProgramResolver resolver(&search);
    const auto result = resolver.resolve(L"simpilot-search-only.exe");
    std::filesystem::remove_all(root);
    require(result && *result == newer, "Everything fallback and candidate ordering");
}

void resolver_uses_user_selection_for_multiple_everything_candidates() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-candidate-selection-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto old_version = root / L"old-version" / L"candidate-choice.exe";
    const auto alphabetical_first = root / L"alpha" / L"candidate-choice.exe";
    const auto alphabetical_second = root / L"beta" / L"candidate-choice.exe";
    const auto newest = root / L"newest" / L"candidate-choice.exe";
    for (const auto& path : {
             old_version, alphabetical_first, alphabetical_second, newest}) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path).put('\0');
    }

    const auto base_time = std::filesystem::file_time_type::clock::now();
    FakeProgramSearch search;
    search.candidates = {
        {old_version, 1, base_time + std::chrono::seconds(2)},
        {alphabetical_second, 2, base_time},
        {newest, 2, base_time + std::chrono::seconds(1)},
        {alphabetical_first, 2, base_time},
    };
    simpilot::ProgramResolutionCache cache(root / L"program-cache.tsv");
    auto selector_called = false;
    const simpilot::ProgramResolver resolver(
        &search, &cache,
        [&](const std::wstring& executable,
            const std::vector<simpilot::ProgramCandidate>& candidates)
            -> std::optional<std::filesystem::path> {
            selector_called = true;
            require(executable == L"candidate-choice.exe", "Pass executable to selector");
            require(candidates.size() == 4
                    && candidates[0].path == newest
                    && candidates[1].path == alphabetical_first
                    && candidates[2].path == alphabetical_second
                    && candidates[3].path == old_version,
                    "Sort candidates before asking the user");
            return alphabetical_second;
        });
    const auto selected = resolver.resolve(L"candidate-choice.exe");
    require(selector_called && selected && *selected == alphabetical_second,
            "Use the candidate selected by the user");

    FakeProgramSearch unavailable;
    unavailable.available_value = false;
    const simpilot::ProgramResolver cached_resolver(&unavailable, &cache);
    const auto cached = cached_resolver.resolve(L"CANDIDATE-CHOICE.EXE");
    require(cached && std::filesystem::equivalent(*cached, alphabetical_second),
            "Reuse the selected candidate from the persistent cache");
    std::filesystem::remove_all(root);
}

void program_resolution_cache_persists_and_removes_stale_entries() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-cache-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto program = root / L"\u5de5\u5177.exe";
    const auto cache_file = root / L"program-cache.tsv";
    std::ofstream(program).put('\0');
    {
        simpilot::ProgramResolutionCache cache(cache_file);
        cache.store(L" Tool.EXE ", program);
        require_equal(cache.size(), std::size_t{1}, "Cache stores one entry");
    }
    {
        std::ifstream stream(cache_file, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(stream)),
                                  std::istreambuf_iterator<char>());
        require(content.starts_with("# Simpilot program resolution cache v2\r\n"),
                "Write the current program cache format");
    }
    {
        simpilot::ProgramResolutionCache cache(cache_file);
        const auto cached = cache.find(L"tool.exe");
        require(cached && std::filesystem::equivalent(*cached, program),
                "Cache persists UTF-8 paths and case-insensitive keys");
        std::filesystem::remove(program);
        require(!cache.find(L"TOOL.EXE"), "Cache removes missing paths");
        require_equal(cache.size(), std::size_t{0}, "Stale cache entry removed");
    }

    const auto legacy_program = root / L"legacy.exe";
    const auto legacy_cache_file = root / L"legacy-program-cache.tsv";
    std::ofstream(legacy_program).put('\0');
    {
        simpilot::ProgramResolutionCache cache(legacy_cache_file);
        cache.store(L"legacy.exe", legacy_program);
    }
    {
        std::ifstream stream(legacy_cache_file, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());
        const auto version = content.find("v2");
        require(version != std::string::npos, "Find cache format version");
        content.replace(version, 2, "v1");
        std::ofstream output(legacy_cache_file, std::ios::binary | std::ios::trunc);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    {
        simpilot::ProgramResolutionCache cache(legacy_cache_file);
        require_equal(cache.size(), std::size_t{0}, "Ignore the old automatic cache format");
    }
    std::filesystem::remove_all(root);
}

void resolver_uses_persistent_cache_without_everything() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-resolver-cache-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto program = root / L"cached-only.exe";
    std::ofstream(program).put('\0');
    simpilot::ProgramResolutionCache cache(root / L"program-cache.tsv");
    cache.store(L"cached-only.exe", program);
    FakeProgramSearch unavailable;
    unavailable.available_value = false;
    const simpilot::ProgramResolver resolver(&unavailable, &cache);
    const auto resolved = resolver.resolve(L"CACHED-ONLY.EXE");
    require(resolved && std::filesystem::equivalent(*resolved, program),
            "Resolver uses the persistent cache while Everything is unavailable");
    std::filesystem::remove_all(root);
}

void logger_removes_entries_older_than_ninety_days_at_startup() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-log-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto log_path = root / L"Simpilot.log";
    {
        std::ofstream seed(log_path, std::ios::binary | std::ios::trunc);
        seed << "2000-01-01T00:00:00.000 expired\r\n"
             << "2099-01-01T00:00:00.000 retained\r\n";
    }
    simpilot::Logger logger(log_path);
    logger.write(L"ready");
    std::ifstream stream(log_path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    require(content.find("expired") == std::string::npos,
            "Logger removes entries older than ninety days");
    require(content.find("retained") != std::string::npos,
            "Logger keeps entries within the retention window");
    require(content.find("ready") != std::string::npos,
            "Logger appends to the retained single log file");
    require(!std::filesystem::exists(log_path.wstring() + L".old"),
            "Logger does not create a rotated log file");
    stream.close();
    require(!std::filesystem::exists(log_path.wstring() + L".tmp"),
            "Logger removes its retention cleanup temporary file");
    std::filesystem::remove_all(root);
}

void config_watcher_reports_menu_file_changes() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-watcher-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    std::mutex mutex;
    std::condition_variable changed_condition;
    bool changed = false;
    simpilot::ConfigWatcher watcher(
        root, {L"Simpilot.ini", L"Simpilot2.ini"},
        [&] {
            {
                std::scoped_lock lock(mutex);
                changed = true;
            }
            changed_condition.notify_one();
        }, {}, std::chrono::milliseconds(100));
    require(watcher.start(), "Config watcher starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    simpilot::write_configuration_text(root / L"Simpilot.ini", L"Tool|notepad.exe\r\n");
    std::unique_lock lock(mutex);
    require(changed_condition.wait_for(lock, std::chrono::seconds(3), [&] { return changed; }),
            "Config watcher reports a menu file change");
    lock.unlock();
    watcher.stop();
    std::filesystem::remove_all(root);
}

void bundled_everything_sdk_exports_load() {
    std::wstring executable(MAX_PATH, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    require(length > 0 && length < executable.size(), "Test executable path");
    executable.resize(length);
    const auto library = std::filesystem::path(executable).parent_path()
        / L"Everything" / L"Everything64.dll";
    const auto search = simpilot::EverythingSearch::try_create(library);
    require(search != nullptr, "Bundled Everything SDK exports");
}

void localization_switches_and_persists_language() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-language-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto default_language = simpilot::Localization::load(root);
    require(default_language.language() == simpilot::UiLanguage::simplified_chinese,
            "Missing language preference defaults to Simplified Chinese");
    simpilot::write_configuration_text(root / L"language.txt", L"invalid-locale\n");
    const auto invalid_language = simpilot::Localization::load(root);
    require(invalid_language.language() == simpilot::UiLanguage::simplified_chinese,
            "Invalid language preference defaults to Simplified Chinese");

    simpilot::Localization localization(simpilot::UiLanguage::simplified_chinese);
    require_equal(std::wstring(localization.text(simpilot::UiText::exit)),
                  std::wstring(L"\u9000\u51fa"), "Chinese UI text");
    require(localization.save(root), "Save language preference");
    const auto loaded = simpilot::Localization::load(root);
    require(loaded.language() == simpilot::UiLanguage::simplified_chinese,
            "Load language preference");

    simpilot::Localization english(simpilot::UiLanguage::english);
    require_equal(std::wstring(english.text(simpilot::UiText::reload_menu)),
                  std::wstring(L"Reload menu"), "English UI text");

    constexpr std::array supported_languages{
        simpilot::UiLanguage::english,
        simpilot::UiLanguage::simplified_chinese,
        simpilot::UiLanguage::traditional_chinese,
    };
    for (const auto language : supported_languages) {
        const simpilot::Localization catalog(language);
        for (int value = 0;
             value <= static_cast<int>(simpilot::UiText::calculator); ++value) {
            require(catalog.text(static_cast<simpilot::UiText>(value))
                        != L"[missing translation]",
                    "Every general UI resource key is translated");
        }
        for (int value = 0;
             value <= static_cast<int>(simpilot::SettingsText::applied_text); ++value) {
            require(catalog.text(static_cast<simpilot::SettingsText>(value))
                        != L"[missing translation]",
                    "Every settings resource key is translated");
        }
        for (int value = 0;
             value <= static_cast<int>(simpilot::CustomHotKeyText::invalid_target_text);
             ++value) {
            require(catalog.text(static_cast<simpilot::CustomHotKeyText>(value))
                        != L"[missing translation]",
                    "Every custom hotkey resource key is translated");
        }
    }

    simpilot::Localization traditional(simpilot::UiLanguage::traditional_chinese);
    require_equal(std::wstring(traditional.text(simpilot::UiText::exit)),
                  std::wstring(L"\u7d50\u675f"), "Traditional Chinese UI text");
    require(traditional.save(root), "Save Traditional Chinese preference");
    const auto reloaded_traditional = simpilot::Localization::load(root);
    require(reloaded_traditional.language() == simpilot::UiLanguage::traditional_chinese,
            "Load Traditional Chinese preference");
    require_equal(std::string(simpilot::Localization::language_code(
                      simpilot::UiLanguage::traditional_chinese)),
                  std::string("zh-TW"), "Traditional Chinese language code");

    const auto fallback_resources = root / L"fallback" / L"Languages";
    simpilot::write_configuration_text(
        fallback_resources / L"en-US.json",
        LR"({"locale":"en-US","strings":{"test.fallback":"English fallback"}})");
    simpilot::write_configuration_text(
        fallback_resources / L"zh-TW.json", L"{ invalid JSON");
    const simpilot::Localization fallback(
        simpilot::UiLanguage::traditional_chinese, fallback_resources);
    require_equal(std::wstring(fallback.text("test.fallback")),
                  std::wstring(L"English fallback"),
                  "Malformed selected catalog falls back to English");
    require_equal(std::wstring(fallback.text("test.missing")),
                  std::wstring(L"[missing translation]"),
                  "Missing English key uses non-empty safety text");
    std::filesystem::remove_all(root);
}

void hotkeys_display_canonical_gestures() {
    const simpilot::HotKeyGesture gesture{MOD_CONTROL | MOD_ALT, VK_SPACE};
    require_equal(gesture.display_text(), std::wstring(L"Ctrl+Alt+Space"),
                  "Canonical hotkey display");
    const simpilot::HotKeyGesture function_key{MOD_WIN | MOD_SHIFT, VK_F12};
    require_equal(function_key.display_text(), std::wstring(L"Shift+Win+F12"),
                  "Canonical function-key display");
    const simpilot::HotKeyGesture backtick{0, VK_OEM_3};
    require_equal(backtick.display_text(), std::wstring(L"`"),
                  "Backtick hotkey display");
    require(simpilot::HotKeyGesture::is_modifier_key(VK_LCONTROL), "Modifier recognition");
    require(!simpilot::HotKeyGesture::is_modifier_key(L'A'), "Regular key recognition");
    require(simpilot::is_supported_windows_letter_hotkey({MOD_WIN, L'G'}),
            "Win+letter supports automatic Windows action override");
    require(!simpilot::is_supported_windows_letter_hotkey({MOD_WIN, L'L'}),
            "Win+L remains unsupported");
    require(!simpilot::is_supported_windows_letter_hotkey({MOD_WIN | MOD_SHIFT, L'G'}),
            "Only an exact Win+letter gesture uses the automatic system override");
}

void app_settings_persist_captured_hotkeys_and_force_override() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-settings-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"Simpilot.settings.ini";
    simpilot::AppSettings settings;
    settings.start_with_windows = true;
    settings.menu_theme = simpilot::MenuTheme::dark;
    settings.main_menu = simpilot::BuiltInHotKey{
        .binding = {simpilot::HotKeyGesture{MOD_CONTROL, VK_MEDIA_PLAY_PAUSE}, true},
        .enabled = true,
    };
    settings.second_menu = simpilot::BuiltInHotKey{
        .binding = {simpilot::HotKeyGesture{MOD_WIN | MOD_SHIFT, VK_F12}, false},
        .enabled = true,
    };
    settings.open_settings = simpilot::BuiltInHotKey{
        .binding = {simpilot::HotKeyGesture{MOD_WIN, L'L'}, true},
        .enabled = true,
    };
    settings.everything_search = simpilot::BuiltInHotKey{
        .binding = {{simpilot::HotKeyGesture{MOD_WIN, L'Q'}}, false},
        .enabled = true,
    };
    settings.disabled_windows_hotkeys[0] = true;
    settings.disabled_windows_hotkeys[5] = true;
    settings.disabled_windows_hotkeys[6] = true;
    settings.disabled_windows_hotkeys[11] = true;
    settings.disabled_windows_hotkeys[25] = true;
    settings.custom_global_hotkeys = {
        simpilot::CustomGlobalHotKey{
            .binding = {{simpilot::HotKeyGesture{MOD_WIN, L'G'}}, false},
            .program_path = L"C:\\工具\\应用.exe",
            .arguments = L"--名称=简驭 --quiet",
            .working_directory = L"C:\\工具",
            .run_as_administrator = true,
            .existing_process_action = simpilot::ExistingProcessAction::start_new_instance,
            .visibility = simpilot::LaunchVisibility::maximized,
        },
        simpilot::CustomGlobalHotKey{
            .binding = {{simpilot::HotKeyGesture{MOD_CONTROL | MOD_ALT, L'X'}}, true},
            .action = simpilot::CustomHotKeyAction::open_file,
            .program_path = L"D:\\Documents\\Guide.pdf",
            .arguments = L"--value \"two words\"",
            .working_directory = L"",
            .run_as_administrator = false,
            .existing_process_action = simpilot::ExistingProcessAction::do_nothing,
            .visibility = simpilot::LaunchVisibility::hidden,
            .enabled = false,
        },
        simpilot::CustomGlobalHotKey{
            .binding = {{simpilot::HotKeyGesture{MOD_CONTROL | MOD_SHIFT, L'D'}}, false},
            .action = simpilot::CustomHotKeyAction::open_folder,
            .program_path = L"D:\\Documents",
        },
        simpilot::CustomGlobalHotKey{
            .binding = {{simpilot::HotKeyGesture{MOD_WIN, L'L'}}, true},
            .program_path = L"C:\\Windows\\notepad.exe",
        },
    };
    auto linkage = settings;
    linkage.main_menu = simpilot::BuiltInHotKey{
        .binding = {simpilot::HotKeyGesture{MOD_WIN, L'A'}, true},
        .enabled = true,
    };
    require(simpilot::global_hotkey_requires_windows_blocking(linkage, 0),
            "Enabled built-in Win+letter hotkeys require Windows shortcut blocking");
    linkage.main_menu.enabled = false;
    require(!simpilot::global_hotkey_requires_windows_blocking(linkage, 0),
            "Disabled built-in hotkeys do not require Windows shortcut blocking");
    require(simpilot::global_hotkey_requires_windows_blocking(
                settings, static_cast<std::size_t>(L'G' - L'A')),
            "Enabled custom Win+letter hotkeys require Windows shortcut blocking");
    require(!simpilot::global_hotkey_requires_windows_blocking(
                settings, static_cast<std::size_t>(L'L' - L'A')),
            "Win+L is never included in runtime blocking linkage");
    require(simpilot::AppSettingsStore::save(path, settings), "Save application settings");
    {
        std::ifstream saved(path, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(saved)),
                                  std::istreambuf_iterator<char>());
        require(content.find("CustomGlobalHotKey1Action=0") != std::string::npos,
                "Persist open-application action value");
        require(content.find("CustomGlobalHotKey2Action=2") != std::string::npos,
                "Persist open-file action value");
        require(content.find("CustomGlobalHotKey3Action=1") != std::string::npos,
                "Persist open-folder action value");
        require(content.find("MenuTheme=2") != std::string::npos,
                "Persist dark popup-menu theme value");
        require(content.find("EverythingSearchCode=8,81") != std::string::npos
                && content.find("EverythingSearchEnabled=1") != std::string::npos,
                "Persist the built-in Everything Search hotkey without a path");
        require(content.find("MainMenuEnabled=1") != std::string::npos
                && content.find("SecondMenuEnabled=1") != std::string::npos
                && content.find("OpenSettingsEnabled=1") != std::string::npos,
                "Persist explicit enabled state for every built-in hotkey");
        require(content.find("Everything\\Everything.exe") == std::string::npos,
                "Do not store an executable path for the built-in Everything action");
        require(content.find("MainMenu=") == std::string::npos
                && content.find("CustomGlobalHotKey1=") == std::string::npos,
                "Do not persist textual hotkey compatibility fields");
    }
    const auto loaded = simpilot::AppSettingsStore::load(path);
    require(loaded.start_with_windows, "Persist startup setting");
    require(loaded.menu_theme == simpilot::MenuTheme::dark,
            "Load popup-menu theme");
    require_equal(loaded.main_menu, settings.main_menu,
                  "Persist captured media hotkey and override mode");
    require_equal(loaded.second_menu, settings.second_menu,
                  "Persist second menu hotkey");
    require(!loaded.open_settings.binding.gesture && !loaded.open_settings.enabled,
            "Reject Win+L and disable the corresponding built-in hotkey");
    auto expected_everything_search = settings.everything_search;
    expected_everything_search.binding.force_override = true;
    require_equal(loaded.everything_search, expected_everything_search,
                  "Persist the built-in Everything Search hotkey and enabled state");
    auto expected_disabled_windows_hotkeys = settings.disabled_windows_hotkeys;
    expected_disabled_windows_hotkeys[static_cast<std::size_t>(L'L' - L'A')] = false;
    require_equal(loaded.disabled_windows_hotkeys, expected_disabled_windows_hotkeys,
                  "Persist supported runtime Windows hotkey blocking state");
    auto expected_custom_hotkeys = settings.custom_global_hotkeys;
    expected_custom_hotkeys.pop_back();
    expected_custom_hotkeys[0].binding.force_override = true;
    require_equal(loaded.custom_global_hotkeys, expected_custom_hotkeys,
                   "Persist custom actions, normalize Win+letter, and reject Win+L");

    const auto incomplete_path = root / L"incomplete.settings.ini";
    {
        std::ofstream incomplete(incomplete_path, std::ios::binary | std::ios::trunc);
        incomplete << "[CustomGlobalHotkeys]\r\n"
                   << "CustomGlobalHotKeyCount=1\r\n"
                   << "CustomGlobalHotKey1Code=3,88\r\n"
                   << "CustomGlobalHotKey1Program=C:\\Apps\\Incomplete.exe\r\n";
    }
    const auto incomplete = simpilot::AppSettingsStore::load(incomplete_path);
    require(incomplete.custom_global_hotkeys.empty(),
            "Ignore custom hotkeys that do not declare Enabled");
    std::filesystem::remove_all(root);

    const auto defaults = simpilot::AppSettingsStore::load(root / L"missing.ini");
    require(defaults.main_menu.binding.gesture
            && defaults.main_menu.binding.gesture->virtual_key == VK_OEM_3
            && defaults.main_menu.enabled,
            "Default main menu hotkey remains backtick");
    require(defaults.custom_global_hotkeys.empty(),
            "Do not represent Everything Search as a custom hotkey");
    require(defaults.everything_search.binding.gesture
            && defaults.everything_search.binding.gesture->modifiers == MOD_WIN
            && defaults.everything_search.binding.gesture->virtual_key == L'S'
            && defaults.everything_search.binding.force_override
            && !defaults.everything_search.enabled,
            "Provide the disabled built-in Everything Search Win+S hotkey");
}

} // namespace

int wmain() {
    try {
        parser_preserves_hierarchy();
        parser_recognizes_entry_kinds_and_admin_marker();
        menu_access_keys_round_trip();
        menu_writer_round_trips_hierarchy_and_utf8();
        menu_writer_rejects_reserved_admin_suffix_for_normal_items();
        parser_rejects_non_utf8_configuration();
        variables_expand_config_and_windows_environment();
        commands_parse_and_replace_executables();
        resolver_falls_back_to_everything_search();
        resolver_uses_user_selection_for_multiple_everything_candidates();
        program_resolution_cache_persists_and_removes_stale_entries();
        resolver_uses_persistent_cache_without_everything();
        logger_removes_entries_older_than_ninety_days_at_startup();
        config_watcher_reports_menu_file_changes();
        bundled_everything_sdk_exports_load();
        localization_switches_and_persists_language();
        hotkeys_display_canonical_gestures();
        app_settings_persist_captured_hotkeys_and_force_override();
        std::wcout << L"All Simpilot core tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
