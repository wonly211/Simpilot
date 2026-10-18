#pragma once
#include "simpilot/settings_backup.hpp"
#include "simpilot/localization.hpp"
#include <Windows.h>

namespace simpilot {
enum class BackupAction { export_settings, import_settings, migrate_directory };
// Returns true only after staging a restore that requires a restart.
bool configuration_backup_action(HWND owner, const std::filesystem::path& root,
    const Localization&, BackupAction);
// Called before constructing TrayApplication, with the single-instance lock held.
bool prepare_configuration(HINSTANCE, const std::filesystem::path& executable);
void validate_import_configuration(const SettingsSnapshot&);
bool restart_for_restore(const std::filesystem::path& executable);
inline constexpr int restore_restart_exit_code = 42;
}
