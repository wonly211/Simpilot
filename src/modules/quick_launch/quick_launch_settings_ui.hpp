#pragma once
#include "quick_launch_settings.hpp"
#include "menu_editor_window.hpp"
#include "menu_icon_cache.hpp"
#include "simpilot/settings_registry.hpp"

namespace simpilot {
struct MenuIconTarget {
    std::wstring menu_name, display_name, target, custom_key, icon_source;
    MenuEntryKind kind = MenuEntryKind::command;
};
class QuickLaunchSettingsUi {
public:
    virtual ~QuickLaunchSettingsUi() = default;
    virtual std::unique_ptr<ISettingsPage> page(bool icons) = 0;
    virtual bool dirty() const = 0;
    virtual bool validate() = 0;
    virtual bool prepare() = 0;
    virtual bool apply() = 0;
    virtual bool rollback() = 0;
    virtual void finish() = 0;
};
std::shared_ptr<QuickLaunchSettingsUi> make_quick_launch_settings_ui(
    QuickLaunchSettings& draft, const std::filesystem::path& directory,
    const std::filesystem::path& icons, std::function<std::vector<MenuIconTarget>()> targets,
    MenuEditorWindow::ProgramResolutionLookup lookup,
    MenuEditorWindow::ProgramResolutionReselect reselect,
    std::function<void(std::wstring_view)> diagnose);
} // namespace simpilot
