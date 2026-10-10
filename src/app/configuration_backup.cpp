#include "configuration_backup.hpp"
#include "register_builtin_modules.hpp"
#include "simpilot/app_settings.hpp"
#include "simpilot/command.hpp"
#include "simpilot/settings_document.hpp"
#include "simpilot/text_encoding.hpp"
#include "simpilot/hotkey_settings_codec.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <algorithm>
#include <format>
#include <sstream>

namespace simpilot {
namespace {
constexpr wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
std::string startup_snapshot() {
    HKEY key = nullptr;
    auto status = RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND) return "absent";
    if (status != ERROR_SUCCESS) throw std::runtime_error("Cannot back up startup registration");
    DWORD type = 0, size = 0;
    status = RegQueryValueExW(key, L"Simpilot", nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND) { RegCloseKey(key); return "absent"; }
    std::vector<BYTE> data(size);
    if (status == ERROR_SUCCESS) status = RegQueryValueExW(key, L"Simpilot", nullptr, &type, data.data(), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) throw std::runtime_error("Cannot read startup registration");
    std::string result = std::to_string(type) + ":";
    for (const auto value : data) result += std::format("{:02x}", value);
    return result;
}
void restore_startup(const std::string& snapshot) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, run_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        throw std::runtime_error("Cannot restore startup registration");
    }
    LONG status = ERROR_SUCCESS;
    if (snapshot == "absent") {
        status = RegDeleteValueW(key, L"Simpilot");
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    } else {
        try {
            const auto colon = snapshot.find(':');
            if (colon == snapshot.npos || (snapshot.size() - colon - 1) % 2) throw std::runtime_error("Invalid startup snapshot");
            const auto type = static_cast<DWORD>(std::stoul(snapshot.substr(0, colon)));
            std::vector<BYTE> data;
            for (auto i = colon + 1; i < snapshot.size(); i += 2) data.push_back(static_cast<BYTE>(std::stoul(snapshot.substr(i, 2), nullptr, 16)));
            status = RegSetValueExW(key, L"Simpilot", 0, type, data.data(), static_cast<DWORD>(data.size()));
        } catch (...) { RegCloseKey(key); throw; }
    }
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) throw std::runtime_error("Cannot restore startup registration");
}
std::wstring wide_error(const std::exception& error) {
    return decode_utf8(error.what()).value_or(L"Configuration operation failed");
}
std::filesystem::path select_file(HWND owner, bool save, const Localization& localization) {
    wchar_t filename[32768]{};
    if (save) wcscpy_s(filename, L"Simpilot-settings.simpilot-backup");
    const auto title = localization.text(save ? "backup.export" : "backup.import");
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Simpilot backup (*.simpilot-backup)\0*.simpilot-backup\0\0";
    dialog.lpstrFile = filename; dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.lpstrTitle = title.data(); dialog.lpstrDefExt = L"simpilot-backup";
    dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) ? std::filesystem::path(filename) : std::filesystem::path{};
}
std::filesystem::path select_directory(HWND owner, const Localization& localization) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileOpenDialog* dialog = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        dialog->SetTitle(localization.text("backup.migrate").data());
        if (SUCCEEDED(dialog->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}
std::wstring warnings(const SettingsSnapshot& snapshot, const Localization& localization) {
    const auto missing = inspect_builtin_backup(snapshot, true);
    if (missing.empty()) return {};
    std::wstring result = L"\n\n" + std::wstring(localization.text("backup.missing"));
    for (std::size_t i = 0; i < std::min<std::size_t>(missing.size(), 8); ++i) result += L"\n" + missing[i];
    return result;
}
}

void validate_import_configuration(const SettingsSnapshot& snapshot) {
    SettingsBackup::validate(snapshot);
    validate_hotkey_setting(SettingsDocument::parse(snapshot.text("Config/Setting.ini")), L"OpenSettings");
    (void)inspect_builtin_backup(snapshot);
}

bool configuration_backup_action(HWND owner, const std::filesystem::path& root,
    const Localization& localization, BackupAction action) {
    try {
        if (action == BackupAction::export_settings) {
            const auto path = select_file(owner, true, localization);
            if (path.empty()) return false;
            if (path.extension() != L".simpilot-backup") throw std::runtime_error("Use the .simpilot-backup file extension");
            const auto snapshot = SettingsBackup::capture(root, encode_utf8(SIMPILOT_VERSION));
            validate_import_configuration(snapshot);
            SettingsBackup::write(snapshot, path);
            (void)SettingsBackup::read(path);
            MessageBoxW(owner, localization.text("backup.exported").data(), localization.text("ui.app_title").data(), MB_OK | MB_ICONINFORMATION);
            return false;
        }
        const auto path = action == BackupAction::migrate_directory ? select_directory(owner, localization) : select_file(owner, false, localization);
        if (path.empty()) return false;
        if (action == BackupAction::migrate_directory && std::filesystem::equivalent(root, path)) throw std::runtime_error("Select the previous version's directory");
        const auto snapshot = action == BackupAction::migrate_directory
            ? SettingsBackup::capture(path, "legacy-directory") : SettingsBackup::read(path);
        if (snapshot.files.empty()) throw std::runtime_error("No saved settings found");
        validate_import_configuration(snapshot);
        const auto file_count = snapshot.files.size();
        const auto message = std::vformat(localization.text("backup.confirm"), std::make_wformat_args(file_count)) + warnings(snapshot, localization);
        if (MessageBoxW(owner, message.c_str(), localization.text("backup.restore_restart").data(), MB_OKCANCEL | MB_ICONWARNING) != IDOK) return false;
        SettingsBackup::stage(root, snapshot, validate_import_configuration);
        return true;
    } catch (const std::exception& error) {
        const auto message = std::wstring(localization.text("backup.failed")) + L"\n\n" + wide_error(error);
        MessageBoxW(owner, message.c_str(), localization.text("ui.app_title").data(), MB_OK | MB_ICONERROR);
        return false;
    }
}

bool prepare_configuration(HINSTANCE instance, const std::filesystem::path& executable) {
    const auto root = executable.parent_path();
    const Localization localization(GetUserDefaultUILanguage() == 0x0409 ? "en-US" : "zh-CN");
    try {
        if (SettingsBackup::pending(root)) {
            const auto restored = SettingsBackup::recover(root, validate_import_configuration, {
                startup_snapshot,
                [&] {
                    if (!StartupRegistration::apply(AppSettingsStore::load(root / L"Config/Setting.ini").start_with_windows, executable))
                        throw std::runtime_error("Cannot apply startup registration");
                }, restore_startup});
            MessageBoxW(nullptr, localization.text(restored ? "backup.restored" : "backup.rolled_back").data(),
                localization.text("ui.app_title").data(), MB_OK | MB_ICONINFORMATION);
        }
        if (SettingsBackup::has_configuration(root)) {
            validate_import_configuration(SettingsBackup::capture(root, "startup"));
            return true;
        }
        const TASKDIALOG_BUTTON buttons[]{
            {100, localization.text("backup.new_user").data()},
            {101, localization.text("backup.migrate").data()},
            {102, localization.text("backup.import").data()}};
        TASKDIALOGCONFIG dialog{sizeof(dialog)};
        dialog.hInstance = instance; dialog.pszWindowTitle = localization.text("ui.app_title").data();
        dialog.pszMainInstruction = localization.text("backup.welcome").data();
        dialog.pszContent = localization.text("backup.portable").data();
        dialog.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
        dialog.cButtons = static_cast<UINT>(std::size(buttons)); dialog.pButtons = buttons; dialog.nDefaultButton = 100;
        for (;;) {
            int selected = IDCANCEL;
            if (FAILED(TaskDialogIndirect(&dialog, &selected, nullptr, nullptr))) return false;
            if (selected == IDCANCEL) return false;
            if (selected == 100) {
                SettingsDocument document;
                document.set(L"General", L"ConfigurationInitialized", L"1");
                if (!document.save(root / L"Config/Setting.ini")) throw std::runtime_error("Cannot initialize configuration directory");
                return true;
            }
            if (configuration_backup_action(nullptr, root, localization,
                selected == 101 ? BackupAction::migrate_directory : BackupAction::import_settings)) {
                // No module has started yet; complete the same durable restore here.
                return prepare_configuration(instance, executable);
            }
        }
    } catch (const std::exception& error) {
        const auto message = std::wstring(localization.text("backup.startup_failed")) + L"\n\n" + root.wstring() + L"\n" + wide_error(error);
        MessageBoxW(nullptr, message.c_str(), localization.text("ui.app_title").data(), MB_OK | MB_ICONERROR);
        // Corrupt current settings must not make the restore UI unreachable.
        // An unfinished transaction must be repaired first; do not supersede it.
        try {
            if (SettingsBackup::pending(root) && !std::filesystem::exists(root / L"Backups/restore.journal")) {
                if (MessageBoxW(nullptr, localization.text("backup.cancel_pending").data(),
                    localization.text("ui.app_title").data(), MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                    SettingsBackup::cancel_pending(root);
                    return prepare_configuration(instance, executable);
                }
                return false;
            }
            if (!SettingsBackup::pending(root)
                && MessageBoxW(nullptr, localization.text("backup.repair").data(),
                    localization.text("ui.app_title").data(), MB_OKCANCEL | MB_ICONQUESTION) == IDOK
                && configuration_backup_action(nullptr, root, localization, BackupAction::import_settings)) {
                return prepare_configuration(instance, executable);
            }
        } catch (...) {}
        return false;
    }
}

bool restart_for_restore(const std::filesystem::path& executable) {
    auto command = L"\"" + executable.wstring() + L"\" --restore-wait " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
        executable.parent_path().c_str(), &startup, &process)) return false;
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return true;
}
}
