#include "configuration_backup.hpp"
#include <Windows.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace simpilot;
namespace fs = std::filesystem;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool welcome_seen = false;
void CALLBACK first_run_timer(HWND, UINT, UINT_PTR timer, DWORD) {
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
        wchar_t type[32]{}; GetClassNameW(window, type, 32);
        if (std::wstring_view(type) == L"#32770") {
            // Windows 11 TaskDialog buttons are DirectUI elements, not HWND
            // controls. Exercise the public dialog message instead of IDs.
            welcome_seen = true;
            SendMessageW(window, TDM_CLICK_BUTTON, 100, 0);
        }
        return TRUE;
    }, 0);
    if (welcome_seen) KillTimer(nullptr, timer);
}
int main() {
    const auto root = fs::temp_directory_path() / (L"Simpilot-migration-" + std::to_wstring(GetCurrentProcessId()));
    try {
        fs::create_directories(root);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        const auto fresh = root / L"fresh";
        fs::create_directories(fresh);
        const auto timer = SetTimer(nullptr, 0, 30, first_run_timer);
        require(prepare_configuration(GetModuleHandleW(nullptr), fresh / L"Simpilot.exe"), "First run accepts Start fresh");
        KillTimer(nullptr, timer);
        require(welcome_seen, "First run displays its native setup dialog");
        require(SettingsBackup::has_configuration(fresh), "First run creates a portable marker");
        require(prepare_configuration(GetModuleHandleW(nullptr), fresh / L"Simpilot.exe"), "Existing configuration does not show a new-user prompt");
        for (const auto version : {"1.0.8", "1.0.9", "1.0.10"}) {
            const auto source = root / version;
            fs::create_directories(source / "Config");
            std::string setting = "[General]\nLanguage=zh-CN\nStartWithWindows=0\nUnknownFutureOption=keep\n"
                "[KeyboardMappings]\nKeyboardMappingCount=1\nKeyboardMapping1Enabled=1\n";
            setting += std::string(version) == "1.0.9"
                ? "KeyboardMapping1SourceAction=91:91:1\nKeyboardMapping1TargetAction=163:29:1\n"
                : "KeyboardMapping1SourceAction=124:100:0\nKeyboardMapping1TargetAction=144:69:1\n";
            if (std::string(version) == "1.0.10") setting += "KeyboardMapping1Process=:multi-app:\nKeyboardMapping1ProcessCount=2\nKeyboardMapping1Process1=editor.exe\nKeyboardMapping1Process2=notepad.exe\n";
            else setting += "KeyboardMapping1Process=notepad.exe\n";
            std::ofstream(source / "Config/Setting.ini", std::ios::binary) << setting;
            std::ofstream(source / "Config/Simpilot.ini", std::ios::binary) << "Notepad|notepad.exe\n";
            std::ofstream(source / "Config/Simpilot2.ini", std::ios::binary) << "Calculator|calc.exe\n";
            const auto snapshot = SettingsBackup::capture(source, version);
            validate_import_configuration(snapshot);
            const auto target = root / (std::string("new-") + version);
            fs::create_directories(target);
            SettingsBackup::stage(target, snapshot, validate_import_configuration);
            require(SettingsBackup::recover(target, validate_import_configuration), "old version migration completes");
            require(SettingsBackup::capture(source, version).files == snapshot.files, "source folder remains unchanged");
            require(SettingsBackup::capture(target, "new").files == snapshot.files, "all files preserved during migration");
            // Overwriting program files in the same directory must not touch settings.
            std::ofstream(target / "Simpilot.exe", std::ios::binary) << "new-program-placeholder";
            require(SettingsBackup::capture(target, "new").files == snapshot.files, "in-place upgrade retains saved settings");
            SettingsBackup::write(snapshot, root / "backup.simpilot-backup");
            const auto imported = SettingsBackup::read(root / "backup.simpilot-backup");
            validate_import_configuration(imported);
            require(imported.files == snapshot.files, "export and import keep old settings byte-for-byte");
        }
        auto invalid = SettingsSnapshot{"test", {{"Config/Setting.ini", {'b','r','o','k','e','n'}}}};
        bool rejected = false;
        try { validate_import_configuration(invalid); } catch (...) { rejected = true; }
        require(rejected, "corrupt existing configuration cannot become defaults");
        const std::string malformed = "[Hotkeys]\nOpenSettingsCode=2,65damaged\n";
        invalid.files["Config/Setting.ini"] = {malformed.begin(), malformed.end()};
        rejected = false;
        try { validate_import_configuration(invalid); } catch (...) { rejected = true; }
        require(rejected, "partially parsed hotkeys are rejected before restore");
        fs::remove_all(root);
        std::cout << "Legacy migration and configuration preservation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
