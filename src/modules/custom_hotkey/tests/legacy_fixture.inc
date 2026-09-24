    settings.items = {
        simpilot::CustomGlobalHotKey{
            .binding = {{simpilot::HotKeyGesture{MOD_WIN, L'G'}}, false},
            .program_path = L"C:\\工具\\应用.exe",
            .arguments = L"--名称=简驭 --quiet",
            .working_directory = L"C:\\工具",
            .run_as_administrator = true,
            .existing_process_action = simpilot::ExistingProcessAction::start_new_instance,
            .visibility = simpilot::LaunchVisibility::maximized,
        },
        simpilot::CustomGlobalHotKey{
            .binding = {{simpilot::HotKeyGesture{MOD_CONTROL | MOD_ALT, L'X'}}, true},
            .action = simpilot::CustomHotKeyAction::open_file,
            .program_path = L"D:\\Documents\\Guide.pdf",
            .arguments = L"--value \"two words\"",
            .working_directory = L"",
            .run_as_administrator = false,
            .existing_process_action = simpilot::ExistingProcessAction::do_nothing,
            .visibility = simpilot::LaunchVisibility::hidden,
            .enabled = false,
        },
        simpilot::CustomGlobalHotKey{
            .binding = {{simpilot::HotKeyGesture{MOD_CONTROL | MOD_SHIFT, L'D'}}, false},
            .action = simpilot::CustomHotKeyAction::open_folder,
            .program_path = L"D:\\Documents",
        },
        simpilot::CustomGlobalHotKey{
            .binding = {{simpilot::HotKeyGesture{MOD_WIN, L'L'}}, true},
            .program_path = L"C:\\Windows\\notepad.exe",
        },
    };
