#include "simpilot/app_settings.hpp"
#include "simpilot/atomic_file.hpp"
#include "simpilot/command.hpp"
#include "simpilot/config_file.hpp"
#include "config_watcher.hpp"
#include "simpilot/localization.hpp"
#include "simpilot/logger.hpp"
#include "simpilot/hotkey.hpp"
#include "menu_parser.hpp"
#include "menu_writer.hpp"
#include "program_resolver.hpp"
#include "program_cache.hpp"
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

}
int main() { try {
    parser_preserves_hierarchy();
    parser_recognizes_entry_kinds_and_admin_marker();
    menu_access_keys_round_trip();
    menu_writer_round_trips_hierarchy_and_utf8();
    menu_writer_rejects_reserved_admin_suffix_for_normal_items();
    parser_rejects_non_utf8_configuration();
    resolver_falls_back_to_everything_search();
    resolver_uses_user_selection_for_multiple_everything_candidates();
    program_resolution_cache_persists_and_removes_stale_entries();
    resolver_uses_persistent_cache_without_everything();
    config_watcher_reports_menu_file_changes();
    return 0;
} catch (const std::exception& error) { std::cerr << error.what(); return 1; } }
