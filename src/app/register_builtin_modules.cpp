#include "register_builtin_modules.hpp"
#include "keyboard_manager.hpp"
#include <format>

#if SIMPILOT_MODULE_EVERYTHING
#include "everything_module.hpp"
#endif
#if SIMPILOT_MODULE_QUICK_LAUNCH
#include "quick_launch_module.hpp"
#endif
#if SIMPILOT_MODULE_CUSTOM_HOTKEY
#include "custom_hotkey_module.hpp"
#endif
#if SIMPILOT_MODULE_WINDOWS_HOTKEY_BLOCKER
#include "windows_hotkey_blocker_module.hpp"
#endif
#if SIMPILOT_MODULE_MOUSE_LOCATOR
#include "mouse_locator_module.hpp"
#endif
#if SIMPILOT_MODULE_KEYBOARD_MAPPING
#include "keyboard_mapping_module.hpp"
#endif
#if SIMPILOT_MODULE_INPUT_METHOD
#include "input_method_module.hpp"
#endif

namespace simpilot {

std::vector<std::wstring> inspect_builtin_backup(const SettingsSnapshot& snapshot, bool check_environment) {
    std::vector<std::wstring> warnings;
    (void)snapshot; (void)check_environment;
#if SIMPILOT_MODULE_KEYBOARD_MAPPING
    inspect_keyboard_mapping_backup(snapshot);
#endif
#if SIMPILOT_MODULE_CUSTOM_HOTKEY
    inspect_custom_hotkey_backup(snapshot, check_environment, warnings);
#endif
#if SIMPILOT_MODULE_QUICK_LAUNCH
    inspect_quick_launch_backup(snapshot, check_environment, warnings);
#endif
#if SIMPILOT_MODULE_INPUT_METHOD
    inspect_input_method_backup(snapshot, check_environment, warnings);
#endif
    return warnings;
}

void register_builtin_modules(
    [[maybe_unused]] ModuleRegistry& modules,
    [[maybe_unused]] HINSTANCE instance,
    [[maybe_unused]] const std::filesystem::path& executable_directory,
    [[maybe_unused]] const Localization& localization,
    [[maybe_unused]] ProgramSearchRegistry& search,
    [[maybe_unused]] TrayMenuRegistry& tray,
    [[maybe_unused]] UiDispatcher& dispatcher,
    [[maybe_unused]] SettingsRegistry& pages,
    [[maybe_unused]] SettingsParticipantRegistry& participants,
    [[maybe_unused]] HotkeyRegistry& hotkeys,
    [[maybe_unused]] KeyboardManager& keyboard,
    [[maybe_unused]] PopupMenuHost& menus,
    [[maybe_unused]] std::function<bool()> can_open,
    [[maybe_unused]] std::function<void()> refresh_hotkeys,
    [[maybe_unused]] std::function<void(std::wstring_view)> diagnose) {
    SettingsDocument document;
    try {
        document = SettingsDocument::load(executable_directory / L"Config" / L"Setting.ini");
    } catch (...) {
        if (diagnose) diagnose(L"cannot read module configuration; using defaults");
    }
#if SIMPILOT_MODULE_EVERYTHING
    modules.add("everything", make_everything_module(
        executable_directory / L"Everything", localization, search, tray,
        dispatcher, hotkeys, pages, participants, document, diagnose));
#endif
#if SIMPILOT_MODULE_WINDOWS_HOTKEY_BLOCKER
    modules.add("windows_hotkey_blocker", make_windows_hotkey_blocker_module(
        document, pages, participants, hotkeys,
        [&keyboard](const auto& policy) { return keyboard.update(policy); }));
#endif
#if SIMPILOT_MODULE_MOUSE_LOCATOR
    modules.add("mouse_locator", make_mouse_locator_module(
        instance, document, pages, participants, diagnose));
#endif
#if SIMPILOT_MODULE_KEYBOARD_MAPPING
    modules.add("keyboard_mapping", make_keyboard_mapping_module(
        document, pages, participants, keyboard, diagnose));
#endif
#if SIMPILOT_MODULE_CUSTOM_HOTKEY
    modules.add("custom_hotkey", make_custom_hotkey_module(
        document, executable_directory / L"Config", participants, hotkeys, keyboard,
        [&localization, diagnose](HWND owner, std::wstring_view target, DWORD error) {
            if (diagnose) diagnose(std::format(L"launch failed error={} target={}", error, target));
            const auto message = std::vformat(localization.text("ui.launch_failed"),
                                              std::make_wformat_args(target, error));
            MessageBoxW(owner, message.c_str(), localization.text("ui.app_title").data(),
                        MB_OK | MB_ICONERROR);
        }, diagnose));
#endif
#if SIMPILOT_MODULE_QUICK_LAUNCH
    modules.add("quick_launch", make_quick_launch_module(instance, executable_directory, document,
        localization, search, tray, dispatcher, pages, participants, hotkeys,
        menus, std::move(can_open), std::move(refresh_hotkeys), diagnose));
#endif
#if SIMPILOT_MODULE_INPUT_METHOD
    modules.add("input_method", make_input_method_module(
        instance, executable_directory / L"Config", localization, dispatcher,
        pages, participants, diagnose));
#endif
}

} // namespace simpilot
