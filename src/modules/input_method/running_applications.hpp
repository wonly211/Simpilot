#pragma once

#include "simpilot/localization.hpp"
#include "simpilot/ui_dispatcher.hpp"

#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace simpilot::input_method {

struct RunningApplication {
    std::wstring executable_path;
    std::vector<std::wstring> window_titles;
    bool has_visible_window = false;
};

struct ApplicationSnapshot {
    std::vector<RunningApplication> applications;
    DWORD error = ERROR_SUCCESS;
};

using ApplicationEnumerator = std::function<ApplicationSnapshot(std::stop_token)>;

ApplicationSnapshot enumerate_running_applications(std::stop_token stop);
ApplicationSnapshot merge_running_applications(ApplicationSnapshot snapshot);
std::vector<std::size_t> filter_running_applications(
    const ApplicationSnapshot& snapshot, bool include_background, std::wstring_view search);
std::wstring application_window_titles(const RunningApplication& application);

enum class ApplicationSource { cancel, file, running };

// These are module-private UI dependencies; a picker never owns settings.
struct ApplicationSelectionServices {
    UiDispatcher* dispatcher = nullptr;
    ApplicationEnumerator enumerate;
    std::function<ApplicationSource(HWND, HWND, const Localization&)> choose_source;
    std::function<std::optional<std::wstring>(HWND, const Localization&)> choose_file;
};

std::optional<std::wstring> show_running_application_dialog(
    HINSTANCE instance, HWND owner, const Localization& localization,
    const ApplicationSelectionServices& services = {});

ApplicationSource show_application_source_menu(
    HWND owner, HWND anchor, const Localization& localization);
std::optional<std::wstring> choose_application(
    HINSTANCE instance, HWND owner, HWND anchor, const Localization& localization,
    const ApplicationSelectionServices& services);

} // namespace simpilot::input_method
