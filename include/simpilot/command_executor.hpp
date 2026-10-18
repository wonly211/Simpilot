#pragma once

#include <Windows.h>
#include <filesystem>
#include <functional>
#include <string>

namespace simpilot {
enum class ExistingProcessAction { show_window, start_new_instance, do_nothing };
enum class LaunchVisibility { normal, minimized, maximized, hidden };

struct LaunchRequest {
    std::wstring target;
    std::wstring arguments;
    std::wstring working_directory;
    bool application = true;
    bool run_as_administrator = false;
    ExistingProcessAction existing_process_action = ExistingProcessAction::start_new_instance;
    LaunchVisibility visibility = LaunchVisibility::normal;
};

using LaunchErrorSink = std::function<void(HWND, std::wstring_view, DWORD)>;
void execute_command(HWND owner, const LaunchRequest& request, const LaunchErrorSink& error);
std::wstring user_profile_directory();
} // namespace simpilot
