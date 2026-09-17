#include "simpilot/app_settings.hpp"
#include "simpilot/atomic_file.hpp"
#include "simpilot/command.hpp"
#include "simpilot/config_file.hpp"
#include "simpilot/config_watcher.hpp"
#include "simpilot/everything.hpp"
#include "simpilot/localization.hpp"
#include "simpilot/logger.hpp"
#include "simpilot/hotkey.hpp"
#include "simpilot/keyboard_mapping.hpp"
#include "simpilot/menu_parser.hpp"
#include "simpilot/menu_writer.hpp"
#include "simpilot/mouse_shake_detector.hpp"
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
#include <utility>

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
    for (const auto terminal : {
             L"cmd.exe", L"powershell.exe", L"pwsh.exe", L"wt.exe",
             L"C:\\Windows\\System32\\cmd.exe",
             L"C:\\Program Files\\PowerShell\\7\\pwsh.exe",
         }) {
        require(simpilot::is_terminal_executable(terminal),
                "Recognize terminal executables that start in the user profile");
    }
    require(!simpilot::is_terminal_executable(L"notepad.exe"),
            "Do not change the working directory of regular launch items");
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
    const auto candidates = resolver.find_candidates(L"simpilot-search-only.exe");
    require(candidates.size() == 2 && candidates.front().path == newer,
            "Expose valid Everything candidates in resolver order");
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
    const auto replacement = root / L"replacement.exe";
    const auto cache_file = root / L"program-cache.tsv";
    std::ofstream(program).put('\0');
    std::ofstream(replacement).put('\0');
    {
        simpilot::ProgramResolutionCache cache(cache_file);
        cache.store(L" Tool.EXE ", program);
        require_equal(cache.size(), std::size_t{1}, "Cache stores one entry");
        cache.store(L"tool.exe", replacement);
        const auto replaced = cache.find(L"TOOL.EXE");
        require(replaced && std::filesystem::equivalent(*replaced, replacement),
                "Cache replaces a previous program selection");
        require_equal(cache.size(), std::size_t{1}, "Cache replacement keeps one entry");
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
        require(cached && std::filesystem::equivalent(*cached, replacement),
                "Cache persists UTF-8 paths and case-insensitive keys");
        std::filesystem::remove(replacement);
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

void atomic_file_replacements_use_unique_temporary_paths() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-atomic-file-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto path = root / L"Setting.ini";
    {
        simpilot::AtomicFileReplacement first(path);
        simpilot::AtomicFileReplacement second(path);
        require(first.temporary_path() != second.temporary_path(),
                "Concurrent atomic writes reserve different temporary files");
        const auto first_attributes = GetFileAttributesW(first.temporary_path().c_str());
        const auto second_attributes = GetFileAttributesW(second.temporary_path().c_str());
        require(first_attributes != INVALID_FILE_ATTRIBUTES
                    && second_attributes != INVALID_FILE_ATTRIBUTES,
                "Atomic replacement reserves both temporary paths");
        require((first_attributes & FILE_ATTRIBUTE_TEMPORARY) == 0
                    && (second_attributes & FILE_ATTRIBUTE_TEMPORARY) == 0,
                "Atomic replacement does not persist temporary cache attributes");
        {
            std::ofstream stream(first.temporary_path(), std::ios::binary | std::ios::trunc);
            stream << "first";
            require(static_cast<bool>(stream), "Write the first atomic replacement");
        }
        {
            std::ofstream stream(second.temporary_path(), std::ios::binary | std::ios::trunc);
            stream << "second";
            require(static_cast<bool>(stream), "Write the second atomic replacement");
        }
        require(first.commit(), "Commit the first atomic replacement");
        require(second.commit(), "Commit the second atomic replacement");
    }
    std::ifstream stream(path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    require_equal(content, std::string("second"),
                  "The last complete atomic replacement wins");
    stream.close();
    for (const auto& item : std::filesystem::directory_iterator(root)) {
        require(item.path().filename().wstring().find(L".tmp.") == std::wstring::npos,
                "Atomic replacement leaves no temporary file behind");
    }
    std::filesystem::remove_all(root);
}

void config_watcher_reports_menu_file_changes() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-watcher-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    std::mutex mutex;
    std::condition_variable changed_condition;
    int change_count = 0;
    simpilot::ConfigWatcher watcher(
        root, {L"Simpilot.ini", L"Simpilot2.ini"},
        [&] {
            {
                std::scoped_lock lock(mutex);
                ++change_count;
            }
            changed_condition.notify_one();
        }, {}, std::chrono::milliseconds(100));
    require(watcher.start(), "Config watcher starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto menu_path = root / L"Simpilot.ini";
    simpilot::write_configuration_text(menu_path, L"Tool|notepad.exe\r\n");
    std::unique_lock lock(mutex);
    require(changed_condition.wait_for(lock, std::chrono::seconds(3),
                                       [&] { return change_count >= 1; }),
            "Config watcher reports a menu file change");
    lock.unlock();

    const auto original_size = std::filesystem::file_size(menu_path);
    const auto original_write_time = std::filesystem::last_write_time(menu_path);
    simpilot::write_configuration_text(menu_path, L"Tool|wordpad.exe\r\n");
    require(std::filesystem::file_size(menu_path) == original_size,
            "Watcher regression uses a same-size configuration replacement");
    std::filesystem::last_write_time(menu_path, original_write_time);
    lock.lock();
    require(changed_condition.wait_for(lock, std::chrono::seconds(3),
                                       [&] { return change_count >= 2; }),
            "Config watcher detects changed content with identical size and timestamp");
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

void localization_resources_cover_supported_languages() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-language-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    simpilot::Localization localization(simpilot::UiLanguage::simplified_chinese);
    require_equal(std::wstring(localization.text(simpilot::UiText::exit)),
                  std::wstring(L"\u9000\u51fa"), "Chinese UI text");
    require_equal(std::wstring(localization.text(simpilot::UiText::main_menu)),
                  std::wstring(L"\u7f16\u8f91\u4e3b\u83dc\u5355"), "Simplified Chinese menu label");
    simpilot::Localization english(simpilot::UiLanguage::english);
    require_equal(std::wstring(english.text(simpilot::UiText::reload_menu)),
                  std::wstring(L"Reload menu"), "English UI text");
    require_equal(std::wstring(english.text(simpilot::UiText::edit_menus)),
                  std::wstring(L"Edit menus..."), "English menu editor label");

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
        for (const auto key : {
                 "settings.section.cursor_locator",
                 "settings.cursor_locator",
                 "settings.cursor_locator.description",
                 "ui.cursor_locator_update_failed",
             }) {
            require(catalog.text(key) != L"[missing translation]",
                    "Every cursor locator resource key is translated");
        }
    }

    simpilot::Localization traditional(simpilot::UiLanguage::traditional_chinese);
    require_equal(std::wstring(traditional.text(simpilot::UiText::exit)),
                  std::wstring(L"\u7d50\u675f"), "Traditional Chinese UI text");
    require_equal(std::wstring(traditional.text(simpilot::UiText::second_menu)),
                  std::wstring(L"\u7de8\u8f2f\u7b2c\u4e8c\u9078\u55ae"), "Traditional Chinese menu label");
    require_equal(std::string(simpilot::Localization::language_code(
                      simpilot::UiLanguage::traditional_chinese)),
                  std::string("zh-TW"), "Traditional Chinese language code");

    const auto fallback_resources = root / L"fallback" / L"Languages";
    simpilot::write_configuration_text(
        fallback_resources / L"en-US.json",
        LR"({"locale":"en-US","strings":{"test.fallback":"English fallback","ui.create_menu_confirm":"Create it now?"}})");
    simpilot::write_configuration_text(
        fallback_resources / L"zh-TW.json", L"{ invalid JSON");
    const simpilot::Localization fallback(
        simpilot::UiLanguage::traditional_chinese, fallback_resources);
    require_equal(std::wstring(fallback.text("test.fallback")),
                  std::wstring(L"English fallback"),
                  "Malformed selected catalog falls back to English");
    require_equal(std::wstring(fallback.text(simpilot::UiText::create_menu_confirm)),
                  std::wstring(L"Create it now?"),
                  "Missing selected-language UI text falls back to English");
    require_equal(std::wstring(fallback.text("test.missing")),
                  std::wstring(L"[missing translation]"),
                  "Missing English key uses non-empty safety text");
    std::filesystem::remove_all(root);
}

void mouse_shake_detector_requires_fast_direction_changes() {
    using Detector = simpilot::MouseShakeDetector;
    const auto start = Detector::Clock::time_point{};
    const auto at = [start](const int milliseconds) {
        return start + std::chrono::milliseconds(milliseconds);
    };

    Detector rapid;
    require(!rapid.update(0, 0, at(0)), "Initialize mouse shake detector");
    require(!rapid.update(70, 0, at(50)), "First fast movement does not trigger");
    require(!rapid.update(-70, 0, at(100)), "First reversal does not trigger");
    require(!rapid.update(70, 0, at(150)), "Second reversal does not trigger");
    require(!rapid.update(-70, 0, at(200)), "Third reversal does not trigger");
    require(rapid.update(70, 0, at(250)),
            "Fast repeated reversals trigger pointer highlighting");
    require(!rapid.update(-70, 0, at(300))
            && !rapid.update(70, 0, at(350))
            && !rapid.update(-70, 0, at(400))
            && !rapid.update(70, 0, at(450)),
            "Cooldown prevents repeated pointer highlighting");

    Detector one_direction;
    require(!one_direction.update(0, 0, at(0)), "Initialize one-direction sample");
    for (int index = 1; index <= 8; ++index) {
        require(!one_direction.update(index * 50, 0, at(index * 50)),
                "One-direction movement never counts as a shake");
    }

    Detector slow;
    require(!slow.update(0, 0, at(0)), "Initialize slow sample");
    for (int index = 1; index <= 6; ++index) {
        const auto x = index % 2 == 0 ? -70 : 70;
        require(!slow.update(x, 0, at(index * 300)),
                "Slow reversals reset instead of triggering");
    }

    Detector small;
    require(!small.update(0, 0, at(0)), "Initialize small movement sample");
    for (int index = 1; index <= 20; ++index) {
        require(!small.update(index % 2 == 0 ? 0 : 3, 0, at(index * 20)),
                "Small pointer jitter does not trigger highlighting");
    }
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
    const auto path = root / L"Setting.ini";
    simpilot::AppSettings settings;
    settings.language = simpilot::UiLanguage::traditional_chinese;
    settings.start_with_windows = true;
    settings.mouse_shake_locator_enabled = true;
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
    simpilot::KeyboardMappingRule single_mapping;
    single_mapping.trigger.single_key = true;
    single_mapping.trigger.action = {VK_F13, 0x64, false};
    single_mapping.output.single_key = true;
    single_mapping.output.action = {VK_F14, 0x65, false};
    single_mapping.purpose = L"Simulate Copilot";
    single_mapping.process_name = L"editor.exe";
    single_mapping.exact_match = true;
    settings.keyboard_mappings_enabled = true;
    settings.keyboard_mappings = {single_mapping};
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
        require(content.find("MouseShakeLocatorEnabled=1") != std::string::npos,
                "Persist mouse shake cursor locator state");
        require(content.find("Language=zh-TW") != std::string::npos,
                "Persist the UI language in the unified settings file");
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
        require(content.find("[KeyboardMappings]") != std::string::npos
                && content.find("KeyboardMappingCount=1") != std::string::npos
                && content.find("KeyboardMapping1Purpose=Simulate Copilot") != std::string::npos
                && content.find("KeyboardMapping1SourceAction=124:100:0") != std::string::npos
                && content.find("KeyboardMapping1TargetAction=125:101:0") != std::string::npos
                && content.find("KeyboardMapping1Process=editor.exe") != std::string::npos,
                "Persist physical keyboard mapping fields");
    }
    const auto loaded = simpilot::AppSettingsStore::load(path);
    require(loaded.language == simpilot::UiLanguage::traditional_chinese,
            "Load the UI language from the unified settings file");
    require(loaded.start_with_windows, "Persist startup setting");
    require(loaded.mouse_shake_locator_enabled,
            "Load mouse shake cursor locator state");
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
    require(loaded.keyboard_mappings_enabled && loaded.keyboard_mappings == settings.keyboard_mappings,
            "Round-trip physical keyboard mappings");

    const std::array language_cases{
        std::pair{simpilot::UiLanguage::simplified_chinese, std::string("zh-CN")},
        std::pair{simpilot::UiLanguage::traditional_chinese, std::string("zh-TW")},
        std::pair{simpilot::UiLanguage::english, std::string("en-US")},
    };
    for (const auto& [language, code] : language_cases) {
        auto language_settings = settings;
        language_settings.language = language;
        const auto language_path = root / (L"language-" + std::wstring(
            code.begin(), code.end()) + L".ini");
        require(simpilot::AppSettingsStore::save(language_path, language_settings),
                "Save every supported UI language");
        require(simpilot::AppSettingsStore::load(language_path).language == language,
                "Load every supported UI language");
    }

    const auto incomplete_path = root / L"incomplete.ini";
    {
        std::ofstream incomplete(incomplete_path, std::ios::binary | std::ios::trunc);
        incomplete << "[CustomGlobalHotkeys]\r\n"
                   << "Language=invalid-locale\r\n"
                   << "CustomGlobalHotKeyCount=1\r\n"
                   << "CustomGlobalHotKey1Code=3,88\r\n"
                   << "CustomGlobalHotKey1Program=C:\\Apps\\Incomplete.exe\r\n";
    }
    const auto incomplete = simpilot::AppSettingsStore::load(incomplete_path);
    require(incomplete.custom_global_hotkeys.empty(),
            "Ignore custom hotkeys that do not declare Enabled");
    require(incomplete.language == simpilot::UiLanguage::simplified_chinese,
            "Invalid language values default to Simplified Chinese");
    std::filesystem::remove_all(root);

    const auto defaults = simpilot::AppSettingsStore::load(root / L"missing.ini");
    require(defaults.language == simpilot::UiLanguage::simplified_chinese,
            "Missing settings default to Simplified Chinese");
    require(!defaults.mouse_shake_locator_enabled,
            "Mouse shake cursor locator is disabled by default");
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

void app_settings_rejects_malformed_keyboard_mappings() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-malformed-mapping-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto path = root / L"Setting.ini";

    simpilot::AppSettings malformed;
    simpilot::KeyboardMappingRule invalid;
    invalid.trigger.modifier_count = invalid.trigger.modifiers.size() + 1;
    invalid.trigger.action = {VK_F13, 0x64, false};
    invalid.output.single_key = true;
    invalid.output.action = {VK_F14, 0x65, false};
    malformed.keyboard_mappings = {invalid};
    require(!simpilot::validate_keyboard_mappings(malformed.keyboard_mappings).empty(),
            "Malformed mapping must be rejected by validation");
    require(!simpilot::AppSettingsStore::save(path, malformed),
            "Settings save must reject malformed mapping data");
    require(!std::filesystem::exists(path),
            "Rejected mapping must not create a settings file");

    simpilot::KeyboardMappingRule canonical;
    canonical.trigger.single_key = true;
    canonical.trigger.action = {VK_F13, 0x64, false};
    canonical.output.single_key = true;
    canonical.output.action = {VK_F14, 0x65, false};
    canonical.process_name = L"Editor";
    malformed.keyboard_mappings = {canonical};
    require(simpilot::AppSettingsStore::save(path, malformed),
            "Valid mapping with a bare process name must save");
    {
        std::ifstream saved(path, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(saved)),
                                  std::istreambuf_iterator<char>());
        require(content.find("KeyboardMapping1Process=editor.exe") != std::string::npos,
                "Saved process scope must use a canonical executable basename");
    }
    const auto loaded = simpilot::AppSettingsStore::load(path);
    require(loaded.keyboard_mappings.size() == 1
                && loaded.keyboard_mappings.front().process_name == L"editor.exe",
            "Canonical process scope must round-trip");

    simpilot::KeyboardMappingRule copilot;
    copilot.trigger.single_key = true;
    copilot.trigger.action = {VK_RCONTROL, 0x1D, true};
    copilot.output.modifier_count = 2;
    copilot.output.modifiers[0] = {VK_LSHIFT, 0x2A, false};
    copilot.output.modifiers[1] = {VK_LWIN, 0x5B, true};
    copilot.output.action = {VK_F23, 0x6E, false};
    malformed.keyboard_mappings = {copilot};
    require(simpilot::validate_keyboard_mappings(
                malformed.keyboard_mappings).empty(),
            "A sided modifier must be valid as a standalone physical source");
    require(simpilot::AppSettingsStore::save(path, malformed),
            "A Right Ctrl to Copilot mapping must save");
    const auto loaded_copilot = simpilot::AppSettingsStore::load(path);
    require(loaded_copilot.keyboard_mappings == malformed.keyboard_mappings,
            "A Right Ctrl to Copilot mapping must round-trip unchanged");

    auto generic_modifier = copilot;
    generic_modifier.trigger.action = {VK_CONTROL, 0x1D, false};
    require(!simpilot::validate_keyboard_mappings({generic_modifier}).empty(),
            "A generic Ctrl identity must not be accepted as a physical source");

    const auto malformed_config = root / L"malformed.ini";
    {
        std::ofstream stream(malformed_config, std::ios::binary | std::ios::trunc);
        stream << "[KeyboardMappings]\r\n"
               << "KeyboardMappingCount=1\r\n"
               << "KeyboardMapping1Enabled=1\r\n"
               << "KeyboardMapping1SourceAction=124:100junk:0\r\n"
               << "KeyboardMapping1TargetAction=125:101:0\r\n";
    }
    const auto skipped = simpilot::AppSettingsStore::load(malformed_config);
    require(skipped.keyboard_mappings.empty(),
            "Malformed mapping tokens must be skipped while loading settings");

    const auto isolated_config = root / L"isolated-invalid-mapping.ini";
    {
        std::ofstream stream(isolated_config, std::ios::binary | std::ios::trunc);
        stream << "[General]\r\nStartWithWindows=1\r\n"
               << "[KeyboardMappings]\r\n"
               << "KeyboardMappingCount=2\r\n"
               << "KeyboardMapping1Enabled=1\r\n"
               << "KeyboardMapping1SourceAction=65:30:0\r\n"
               << "KeyboardMapping1TargetAction=162:29:0\r\n"
               << "KeyboardMapping2Enabled=1\r\n"
               << "KeyboardMapping2SourceAction=65:30:0\r\n"
               << "KeyboardMapping2TargetAction=66:48:0\r\n";
    }
    std::vector<std::wstring> diagnostics;
    const auto isolated = simpilot::AppSettingsStore::load(
        isolated_config,
        [&diagnostics](const std::wstring_view message) {
            diagnostics.emplace_back(message);
        });
    require(isolated.start_with_windows,
            "An invalid mapping must not affect unrelated settings");
    require(isolated.keyboard_mappings.size() == 1
                && isolated.keyboard_mappings.front().output.action
                    == simpilot::PhysicalKey{L'B', 48, false},
            "A semantic error in one mapping must not reject a later valid mapping");
    require(!diagnostics.empty(),
            "Skipped keyboard mappings must report a diagnostic");
    std::filesystem::remove_all(root);
}

void keyboard_mapping_validation_is_order_insensitive() {
    const simpilot::PhysicalKey left_ctrl{VK_LCONTROL, 29, false};
    const simpilot::PhysicalKey left_shift{VK_LSHIFT, 42, false};
    const simpilot::PhysicalKey action{L'A', 30, false};
    const simpilot::PhysicalKey first_target{L'B', 48, false};
    const simpilot::PhysicalKey second_target{L'C', 46, false};

    simpilot::KeyboardMappingRule first;
    first.trigger.modifier_count = 2;
    first.trigger.modifiers[0] = left_ctrl;
    first.trigger.modifiers[1] = left_shift;
    first.trigger.action = action;
    first.output.single_key = true;
    first.output.action = first_target;
    first.process_name = L"Editor.exe";

    auto reordered = first;
    reordered.trigger.modifiers[0] = left_shift;
    reordered.trigger.modifiers[1] = left_ctrl;
    reordered.output.action = second_target;
    require(!simpilot::validate_keyboard_mappings({first, reordered}).empty(),
            "Reordered modifiers must still be detected as duplicate sources");

    auto disabled = reordered;
    disabled.enabled = false;
    require(!simpilot::validate_keyboard_mappings({first, disabled}).empty(),
            "Disabled duplicate sources must be rejected consistently");

    auto chord = first;
    chord.trigger.chord_action = second_target;
    chord.trigger.modifiers[0] = left_shift;
    chord.trigger.modifiers[1] = left_ctrl;
    require(!simpilot::validate_keyboard_mappings({first, chord}).empty(),
            "Reordered modifiers must not bypass prefix validation");

    simpilot::KeyboardMappingRule cycle_start;
    cycle_start.trigger.single_key = true;
    cycle_start.trigger.action = action;
    cycle_start.output.modifier_count = 2;
    cycle_start.output.modifiers[0] = left_ctrl;
    cycle_start.output.modifiers[1] = left_shift;
    cycle_start.output.action = first_target;

    simpilot::KeyboardMappingRule cycle_end;
    cycle_end.trigger.modifier_count = 2;
    cycle_end.trigger.modifiers[0] = left_shift;
    cycle_end.trigger.modifiers[1] = left_ctrl;
    cycle_end.trigger.action = first_target;
    cycle_end.output.single_key = true;
    cycle_end.output.action = action;
    require(!simpilot::validate_keyboard_mappings({cycle_start, cycle_end}).empty(),
            "Reordered modifier sets must still detect mapping cycles");

    auto exact_source = first;
    exact_source.process_name = L"editor.exe";
    exact_source.exact_match = true;
    auto prefix_source = reordered;
    prefix_source.process_name = L"editor.exe";
    prefix_source.exact_match = false;
    require(simpilot::validate_keyboard_mappings(
                {exact_source, prefix_source}).empty(),
            "Exact and prefix process scopes must be distinct rule scopes");

    auto disjoint_chord = chord;
    disjoint_chord.process_name = L"browser.exe";
    require(simpilot::validate_keyboard_mappings(
                {first, disjoint_chord}).empty(),
            "Disjoint exact process scopes must not create a prefix conflict");

    cycle_start.process_name = L"editor.exe";
    cycle_end.process_name = L"browser.exe";
    require(simpilot::validate_keyboard_mappings(
                {cycle_start, cycle_end}).empty(),
            "Disjoint exact process scopes must not create a mapping cycle");
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
        atomic_file_replacements_use_unique_temporary_paths();
        config_watcher_reports_menu_file_changes();
        bundled_everything_sdk_exports_load();
        localization_resources_cover_supported_languages();
        mouse_shake_detector_requires_fast_direction_changes();
        hotkeys_display_canonical_gestures();
        app_settings_persist_captured_hotkeys_and_force_override();
        app_settings_rejects_malformed_keyboard_mappings();
        keyboard_mapping_validation_is_order_insensitive();
        std::wcout << L"All Simpilot core tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
