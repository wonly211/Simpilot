#include "simpilot/app_settings.hpp"
#include "simpilot/atomic_file.hpp"
#include "simpilot/command.hpp"
#include "simpilot/config_file.hpp"
#include "simpilot/localization.hpp"
#include "simpilot/logger.hpp"
#include "simpilot/hotkey.hpp"
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


void localization_resources_cover_supported_languages() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-language-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    simpilot::Localization localization(simpilot::UiLanguage::simplified_chinese);
    require_equal(std::wstring(localization.text("ui.exit")),
                  std::wstring(L"\u9000\u51fa"), "Chinese UI text");
    simpilot::Localization english(simpilot::UiLanguage::english);
    require_equal(std::wstring(english.text("ui.settings")), std::wstring(L"Settings..."), "English settings label");

    constexpr std::array supported_languages{
        simpilot::UiLanguage::english,
        simpilot::UiLanguage::simplified_chinese,
        simpilot::UiLanguage::traditional_chinese,
    };
    for (const auto language : supported_languages) {
        const simpilot::Localization catalog(language);
        for (const auto key : {"ui.app_title", "ui.settings", "ui.about", "ui.exit",
            "settings.title", "settings.applied", "settings.tab.general", "settings.tab.global_hotkeys"}) {
            require(catalog.text(key)
                        != L"[missing translation]",
                    "Every settings resource key is translated");
        }
    }

    simpilot::Localization traditional(simpilot::UiLanguage::traditional_chinese);
    require_equal(std::wstring(traditional.text("ui.exit")),
                  std::wstring(L"\u7d50\u675f"), "Traditional Chinese UI text");
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
    require_equal(std::wstring(fallback.text("ui.create_menu_confirm")),
                  std::wstring(L"Create it now?"),
                  "Missing selected-language UI text falls back to English");
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
    const auto path = root / L"Setting.ini";
    simpilot::AppSettings settings;
    settings.language = simpilot::UiLanguage::traditional_chinese;
    settings.start_with_windows = true;
    settings.open_settings = simpilot::BuiltInHotKey{
        .binding = {simpilot::HotKeyGesture{MOD_WIN, L'L'}, true},
        .enabled = true,
    };
    require(simpilot::AppSettingsStore::save(path, settings), "Save application settings");
    {
        std::ifstream saved(path, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(saved)),
                                  std::istreambuf_iterator<char>());
        require(content.find("Language=zh-TW") != std::string::npos,
                "Persist the UI language in the unified settings file");
        require(content.find("OpenSettingsEnabled=1") != std::string::npos,
                "Persist shell hotkey enabled state");
        require(content.find("Everything\\Everything.exe") == std::string::npos,
                "Do not store an executable path for the built-in Everything action");
        require(content.find("MainMenu=") == std::string::npos
                && content.find("CustomGlobalHotKey1=") == std::string::npos,
                "Do not persist textual hotkey compatibility fields");
    }
    const auto loaded = simpilot::AppSettingsStore::load(path);
    require(loaded.language == simpilot::UiLanguage::traditional_chinese,
            "Load the UI language from the unified settings file");
    require(loaded.start_with_windows, "Persist startup setting");
    require(!loaded.open_settings.binding.gesture && !loaded.open_settings.enabled,
            "Reject Win+L and disable the corresponding built-in hotkey");

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
    require(incomplete.language == simpilot::UiLanguage::simplified_chinese,
            "Invalid language values default to Simplified Chinese");
    std::filesystem::remove_all(root);

    const auto defaults = simpilot::AppSettingsStore::load(root / L"missing.ini");
    require(defaults.language == simpilot::UiLanguage::simplified_chinese,
            "Missing settings default to Simplified Chinese");
    require(!defaults.open_settings.enabled, "Default settings shortcut remains unassigned");
}

} // namespace

int wmain() {
    try {
        variables_expand_config_and_windows_environment();
        commands_parse_and_replace_executables();
        logger_removes_entries_older_than_ninety_days_at_startup();
        atomic_file_replacements_use_unique_temporary_paths();
        localization_resources_cover_supported_languages();
        hotkeys_display_canonical_gestures();
        app_settings_persist_captured_hotkeys_and_force_override();
        std::wcout << L"All Simpilot core tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
