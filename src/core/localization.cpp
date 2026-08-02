#include "simpilot/localization.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>

namespace simpilot {
namespace {

constexpr auto language_file_name = L"language.txt";

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](const unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](const unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

} // namespace

Localization::Localization(const UiLanguage language) : language_(language) {}

Localization Localization::load(const std::filesystem::path& config_directory) {
    std::ifstream stream(config_directory / language_file_name, std::ios::binary);
    if (stream) {
        std::string value((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        value = trim(std::move(value));
        if (value == "zh-CN") return Localization(UiLanguage::simplified_chinese);
        if (value == "en-US") return Localization(UiLanguage::english);
    }
    return Localization(detect_windows_language());
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
    if (language_ == UiLanguage::simplified_chinese) {
        switch (text_value) {
        case UiText::app_title: return L"\u7b80\u9a6d | Simpilot";
        case UiText::menu_two: return L"\u83dc\u5355 2";
        case UiText::reload_menu: return L"\u5237\u65b0\u83dc\u5355";
        case UiText::settings: return L"\u8bbe\u7f6e...";
        case UiText::open_everything: return L"\u6253\u5f00 Everything";
        case UiText::everything_unavailable: return L"\u65e0\u6cd5\u6253\u5f00 Everything \u641c\u7d22\u3002\u8bf7\u786e\u8ba4 Everything \u7ec4\u4ef6\u5b8c\u6574，\u5e76\u67e5\u770b Log\\Simpilot.log \u4e86\u89e3\u8be6\u60c5\u3002";
        case UiText::repair_everything: return L"\u5b89\u88c5/\u4fee\u590d Everything \u670d\u52a1...";
        case UiText::repair_everything_success: return L"Everything \u670d\u52a1\u5df2\u5c31\u7eea\u3002";
        case UiText::repair_everything_failed: return L"Everything \u670d\u52a1\u5b89\u88c5\u6216\u4fee\u590d\u672a\u5b8c\u6210\u3002\u53ef\u67e5\u770b Log\\Simpilot.log \u4e86\u89e3\u8be6\u60c5\u3002";
        case UiText::reload_failed: return L"\u914d\u7f6e\u6682\u65f6\u65e0\u6cd5\u8bfb\u53d6\uff0c\u5df2\u4fdd\u7559\u4e0a\u4e00\u4efd\u53ef\u7528\u83dc\u5355\u3002";
        case UiText::about: return L"\u5173\u4e8e \u7b80\u9a6d | Simpilot";
        case UiText::maintenance: return L"\u7ef4\u62a4";
        case UiText::language: return L"\u8bed\u8a00";
        case UiText::english: return L"English";
        case UiText::simplified_chinese: return L"\u7b80\u4f53\u4e2d\u6587";
        case UiText::exit: return L"\u9000\u51fa";
        case UiText::configuration_header: return L"\u7b80\u9a6d | Simpilot \u914d\u7f6e";
        case UiText::common: return L"\u5e38\u7528";
        case UiText::notepad: return L"\u8bb0\u4e8b\u672c";
        case UiText::calculator: return L"\u8ba1\u7b97\u5668";
        }
    }

    switch (text_value) {
    case UiText::app_title: return L"\u7b80\u9a6d | Simpilot";
    case UiText::menu_two: return L"Menu 2";
    case UiText::reload_menu: return L"Reload menu";
    case UiText::settings: return L"Settings...";
    case UiText::open_everything: return L"Open Everything";
    case UiText::everything_unavailable: return L"Everything Search could not be opened. Verify the Everything components and see Log\\Simpilot.log for details.";
    case UiText::repair_everything: return L"Install/repair Everything service...";
    case UiText::repair_everything_success: return L"The Everything service is ready.";
    case UiText::repair_everything_failed: return L"The Everything service was not installed or repaired. See Log\\Simpilot.log for details.";
    case UiText::reload_failed: return L"The configuration could not be read. The last valid menu was kept.";
    case UiText::about: return L"About \u7b80\u9a6d | Simpilot";
    case UiText::maintenance: return L"Maintenance";
    case UiText::language: return L"Language";
    case UiText::english: return L"English";
    case UiText::simplified_chinese: return L"Simplified Chinese";
    case UiText::exit: return L"Exit";
    case UiText::configuration_header: return L"\u7b80\u9a6d | Simpilot configuration";
    case UiText::common: return L"Common";
    case UiText::notepad: return L"Notepad";
    case UiText::calculator: return L"Calculator";
    }
    return {};
}

std::wstring_view Localization::text(const SettingsText text_value) const noexcept {
    using enum SettingsText;
    if (text_value >= menu_icons_tab_text && text_value <= icon_selection_failed_text) {
        if (language_ == UiLanguage::simplified_chinese) {
            switch (text_value) {
            case menu_icons_tab_text: return L"\u83dc\u5355\u56fe\u6807";
            case menu_icons_heading_text: return L"\u5feb\u6377\u542f\u52a8\u83dc\u5355\u56fe\u6807";
            case menu_icons_scope_text: return L"\u4e3a\u5feb\u6377\u542f\u52a8\u83dc\u5355\u4e2d\u7684\u542f\u52a8\u9879\u6307\u5b9a\u56fe\u6807\u3002\u652f\u6301 ICO \u6587\u4ef6\uff0c\u4e5f\u53ef\u4ece EXE \u6216 DLL \u4e2d\u9009\u62e9\u56fe\u6807\u3002";
            case menu_icon_menu_column_text: return L"\u83dc\u5355";
            case menu_icon_name_column_text: return L"\u540d\u79f0";
            case menu_icon_target_column_text: return L"\u76ee\u6807";
            case menu_icon_source_column_text: return L"\u56fe\u6807\u6765\u6e90";
            case select_icon_text: return L"\u9009\u62e9\u56fe\u6807...";
            case restore_auto_icon_text: return L"\u6062\u590d\u81ea\u52a8\u56fe\u6807";
            case automatic_icon_text: return L"\u81ea\u52a8";
            case custom_icon_text: return L"\u81ea\u5b9a\u4e49";
            case icon_selection_failed_text: return L"\u65e0\u6cd5\u4ece\u6240\u9009\u6587\u4ef6\u63d0\u53d6\u56fe\u6807\u3002";
            default: break;
            }
        }
        switch (text_value) {
        case menu_icons_tab_text: return L"Menu icons";
        case menu_icons_heading_text: return L"Quick-launch menu icons";
        case menu_icons_scope_text: return L"Assign icons to quick-launch menu entries. Choose an ICO file or select an icon from an EXE or DLL.";
        case menu_icon_menu_column_text: return L"Menu";
        case menu_icon_name_column_text: return L"Name";
        case menu_icon_target_column_text: return L"Target";
        case menu_icon_source_column_text: return L"Icon source";
        case select_icon_text: return L"Choose icon...";
        case restore_auto_icon_text: return L"Restore automatic icon";
        case automatic_icon_text: return L"Automatic";
        case custom_icon_text: return L"Custom";
        case icon_selection_failed_text: return L"The selected file does not provide a usable icon.";
        default: break;
        }
    }
    if (language_ == UiLanguage::simplified_chinese) {
        switch (text_value) {
        case title_text: return L"简驭 | Simpilot - 设置";
        case heading_text: return L"常规设置";
        case main_menu_text: return L"快捷启动菜单";
        case second_menu_text: return L"第二菜单";
        case open_settings_text: return L"设置窗口";
        case open_everything_search_text: return L"Everything 搜索";
        case clear_text: return L"清除";
        case startup_text: return L"登录 Windows 后自动启动简驭 | Simpilot";
        case menu_theme_text: return L"快捷启动菜单主题";
        case system_theme_text: return L"跟随 Windows";
        case light_theme_text: return L"浅色";
        case dark_theme_text: return L"深色";
        case hint_text: return L"点击录制按钮，然后按下要使用的组合键。按 Esc 取消录制；关闭开关不会清除已录制的热键。";
        case capture_text: return L"正在记录，请按下热键（Esc 取消）...";
        case capture_failed_text: return L"无法启动系统热键捕获，已退出记录状态。";
        case escape_cancelled_text: return L"已取消本次热键记录。";
        case save_text: return L"保存";
        case apply_text: return L"应用";
        case cancel_text: return L"取消";
        case general_tab_text: return L"常规";
        case custom_hotkeys_tab_text: return L"全局热键";
        case system_hotkeys_tab_text: return L"Windows 快捷键屏蔽";
        case custom_hotkeys_heading_text: return L"全局热键";
        case custom_hotkeys_scope_text: return L"管理简驭的内置热键，以及用于打开应用、文件夹或文件的自定义热键。启用的 Win+字母热键会优先执行自定义操作；Win+L 不可覆盖。";
        case custom_enabled_column_text: return L"启用";
        case custom_hotkey_column_text: return L"热键";
        case custom_action_column_text: return L"操作类型";
        case custom_target_column_text: return L"目标";
        case add_text: return L"添加";
        case edit_text: return L"编辑";
        case delete_text: return L"删除";
        case open_application_text: return L"打开应用";
        case open_folder_text: return L"打开文件夹";
        case open_file_text: return L"打开文件";
        case system_hotkeys_heading_text: return L"屏蔽 Windows 快捷键";
        case system_hotkeys_scope_text: return L"选择要在简驭运行期间屏蔽的 Windows 快捷键。此功能只实时拦截键盘输入，不会修改系统策略或关闭对应的 Windows 功能。";
        case system_hotkeys_runtime_text: return L"点击“应用”或“保存”后立即生效，无需重启资源管理器；退出简驭后自动解除。";
        case win_l_unsupported_text: return L"Win+L 是 Windows 安全锁定快捷键，简驭 | Simpilot 不支持将其设置为全局热键。";
        case quick_launch_tab_text: return L"快捷启动菜单";
        case quick_launch_heading_text: return L"快捷启动菜单";
        case quick_launch_scope_text: return L"管理主菜单和第二菜单中的分类、启动项及顺序。";
        case general_scope_text: return L"设置启动方式和快捷启动菜单外观。";
        case startup_section_text: return L"启动";
        case appearance_section_text: return L"外观";
        case built_in_hotkeys_section_text: return L"简驭功能热键";
        case custom_hotkeys_section_text: return L"自定义热键";
        case unsaved_changes_text: return L"尚有未应用的更改。确定要放弃这些更改吗？";
        case applied_text: return L"已应用";
        }
    }
    switch (text_value) {
    case title_text: return L"\u7b80\u9a6d | Simpilot - Settings";
    case heading_text: return L"General settings";
    case main_menu_text: return L"Quick-launch menu";
    case second_menu_text: return L"Second menu";
    case open_settings_text: return L"Settings window";
    case open_everything_search_text: return L"Everything Search";
    case clear_text: return L"Clear";
    case startup_text: return L"Start \u7b80\u9a6d | Simpilot after signing in to Windows";
    case menu_theme_text: return L"Quick-launch menu theme";
    case system_theme_text: return L"Follow Windows";
    case light_theme_text: return L"Light";
    case dark_theme_text: return L"Dark";
    case hint_text: return L"Select a record button, then press the combination to use. Press Esc to cancel; turning a switch off keeps the recorded hotkey.";
    case capture_text: return L"Recording: press a hotkey (Esc to cancel)...";
    case capture_failed_text: return L"System hotkey capture could not start; recording was cancelled.";
    case escape_cancelled_text: return L"Hotkey capture cancelled.";
    case save_text: return L"Save";
    case apply_text: return L"Apply";
    case cancel_text: return L"Cancel";
    case general_tab_text: return L"General";
    case custom_hotkeys_tab_text: return L"Global hotkeys";
    case system_hotkeys_tab_text: return L"Windows shortcut blocking";
    case custom_hotkeys_heading_text: return L"Global hotkeys";
    case custom_hotkeys_scope_text: return L"Manage built-in Simpilot hotkeys and custom hotkeys for applications, folders, or files. Enabled Win+letter hotkeys take priority over the corresponding Windows shortcut; Win+L cannot be overridden.";
    case custom_enabled_column_text: return L"Enabled";
    case custom_hotkey_column_text: return L"Hotkey";
    case custom_action_column_text: return L"Action type";
    case custom_target_column_text: return L"Target";
    case add_text: return L"Add";
    case edit_text: return L"Edit";
    case delete_text: return L"Delete";
    case open_application_text: return L"Open application";
    case open_folder_text: return L"Open folder";
    case open_file_text: return L"Open file";
    case system_hotkeys_heading_text: return L"Block Windows shortcuts";
    case system_hotkeys_scope_text: return L"Select the Windows shortcuts to block while \u7b80\u9a6d | Simpilot is running. This intercepts keyboard input only; it does not change system policies or disable Windows features.";
    case system_hotkeys_runtime_text: return L"Changes take effect after Apply or Save; Explorer does not need to restart. Blocking ends automatically when \u7b80\u9a6d | Simpilot exits.";
    case win_l_unsupported_text: return L"Win+L is the Windows security lock shortcut and cannot be assigned as a 简驭 | Simpilot global hotkey.";
    case quick_launch_tab_text: return L"Quick-launch menu";
    case quick_launch_heading_text: return L"Quick-launch menu";
    case quick_launch_scope_text: return L"Manage categories, launch items, and ordering in the main and second menus.";
    case general_scope_text: return L"Configure startup behavior and the appearance of the quick-launch menu.";
    case startup_section_text: return L"Startup";
    case appearance_section_text: return L"Appearance";
    case built_in_hotkeys_section_text: return L"Simpilot function hotkeys";
    case custom_hotkeys_section_text: return L"Custom hotkeys";
    case unsaved_changes_text: return L"There are unapplied changes. Discard them?";
    case applied_text: return L"Applied";
    }
    return {};
}

std::wstring_view Localization::text(const CustomHotKeyText text_value) const noexcept {
    using enum CustomHotKeyText;
    if (language_ == UiLanguage::simplified_chinese) {
        switch (text_value) {
        case window_title_text: return L"简驭 | Simpilot - 添加全局热键";
        case edit_window_title_text: return L"简驭 | Simpilot - 编辑全局热键";
        case title_text: return L"添加新的全局热键";
        case edit_title_text: return L"编辑全局热键";
        case trigger_heading_text: return L"热键";
        case trigger_type_text: return L"点击下方区域，然后按下要使用的按键或组合键。";
        case allow_modifiers_text: return L"允许使用 Ctrl、Alt、Shift 或 Win 组合键";
        case action_heading_text: return L"执行操作";
        case open_application_text: return L"打开应用";
        case open_folder_text: return L"打开文件夹";
        case open_file_text: return L"打开文件";
        case program_path_text: return L"程序路径";
        case folder_path_text: return L"文件夹路径";
        case file_path_text: return L"文件路径";
        case arguments_text: return L"参数（可选）";
        case working_directory_text: return L"在目录中启动（可选）";
        case browse_text: return L"浏览...";
        case identity_text: return L"运行权限";
        case normal_identity_text: return L"正常";
        case administrator_identity_text: return L"管理员";
        case existing_process_text: return L"程序已运行时";
        case show_window_text: return L"显示窗口";
        case start_new_text: return L"启动新实例";
        case do_nothing_text: return L"不执行操作";
        case visibility_text: return L"启动后窗口状态";
        case normal_visibility_text: return L"正常";
        case minimized_text: return L"最小化";
        case maximized_text: return L"最大化";
        case hidden_text: return L"隐藏";
        case save_text: return L"保存";
        case cancel_text: return L"取消";
        case modifiers_required_text: return L"当前已关闭“允许组合键”，请只录制一个非修饰键。";
        case win_l_unsupported_text: return L"Win+L 是 Windows 安全锁定快捷键，简驭 | Simpilot 不支持覆盖此组合。";
        case capture_failed_text: return L"无法启动系统级键盘捕获，请稍后重试。";
        case invalid_target_text: return L"请选择一个存在的应用程序、文件夹或文件。";
        }
    }
    switch (text_value) {
    case window_title_text: return L"\u7b80\u9a6d | Simpilot - Add global hotkey";
    case edit_window_title_text: return L"\u7b80\u9a6d | Simpilot - Edit global hotkey";
    case title_text: return L"Add a new global hotkey";
    case edit_title_text: return L"Edit global hotkey";
    case trigger_heading_text: return L"Trigger";
    case trigger_type_text: return L"Click below, then press the key or combination to use.";
    case allow_modifiers_text: return L"Allow Ctrl, Alt, Shift, or Win combinations";
    case action_heading_text: return L"Action";
    case open_application_text: return L"Open application";
    case open_folder_text: return L"Open folder";
    case open_file_text: return L"Open file";
    case program_path_text: return L"Program path";
    case folder_path_text: return L"Folder path";
    case file_path_text: return L"File path";
    case arguments_text: return L"Arguments (optional)";
    case working_directory_text: return L"Start in directory (optional)";
    case browse_text: return L"Browse...";
    case identity_text: return L"Run permission";
    case normal_identity_text: return L"Normal";
    case administrator_identity_text: return L"Administrator";
    case existing_process_text: return L"When already running";
    case show_window_text: return L"Show window";
    case start_new_text: return L"Start a new instance";
    case do_nothing_text: return L"Do nothing";
    case visibility_text: return L"Window state after launch";
    case normal_visibility_text: return L"Normal";
    case minimized_text: return L"Minimized";
    case maximized_text: return L"Maximized";
    case hidden_text: return L"Hidden";
    case save_text: return L"Save";
    case cancel_text: return L"Cancel";
    case modifiers_required_text: return L"Modifier combinations are disabled. Record one non-modifier key.";
    case win_l_unsupported_text: return L"Win+L is the Windows security lock shortcut and cannot be overridden by 简驭 | Simpilot.";
    case capture_failed_text: return L"System-level keyboard capture could not start. Try again.";
    case invalid_target_text: return L"Select an existing application, folder, or file.";
    }
    return {};
}

std::string_view Localization::language_code(const UiLanguage language) noexcept {
    return language == UiLanguage::simplified_chinese ? "zh-CN" : "en-US";
}

UiLanguage Localization::detect_windows_language() noexcept {
    const auto language = GetUserDefaultUILanguage();
    const auto sublanguage = SUBLANGID(language);
    return PRIMARYLANGID(language) == LANG_CHINESE
        && (sublanguage == SUBLANG_CHINESE_SIMPLIFIED || sublanguage == SUBLANG_CHINESE_SINGAPORE)
            ? UiLanguage::simplified_chinese
            : UiLanguage::english;
}

} // namespace simpilot
