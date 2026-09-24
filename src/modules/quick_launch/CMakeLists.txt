add_library(simpilot_module_quick_launch STATIC
    quick_launch_module.cpp quick_launch_settings.cpp quick_launch_settings_ui.cpp
    launch_menu_renderer.cpp menu_editor_window.cpp menu_icon_cache.cpp menu_theme.cpp
    program_selection_dialog.cpp menu_parser.cpp menu_writer.cpp program_cache.cpp
    program_resolver.cpp config_watcher.cpp)
target_include_directories(simpilot_module_quick_launch PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(simpilot_module_quick_launch PRIVATE "${PROJECT_SOURCE_DIR}/resources")
target_compile_definitions(simpilot_module_quick_launch PRIVATE
    UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(simpilot_module_quick_launch PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_module_quick_launch PUBLIC
    simpilot_module_support simpilot_win32_ui simpilot_command_execution comdlg32 shcore)
foreach(locale en-US zh-CN zh-TW)
    set_property(GLOBAL APPEND PROPERTY SIMPILOT_MODULE_LANGUAGE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Languages/${locale}.json")
endforeach()
if(BUILD_TESTING)
    foreach(suite quick_launch_algorithm_tests menu_presentation_tests program_selection_dialog_test quick_launch_module_tests)
        add_executable(simpilot_${suite} tests/${suite}.cpp
            "${PROJECT_BINARY_DIR}/BuiltinLanguagePack.rc" "${PROJECT_SOURCE_DIR}/resources/Simpilot.rc")
        add_dependencies(simpilot_${suite} simpilot_builtin_language_pack)
        target_include_directories(simpilot_${suite} PRIVATE "${PROJECT_SOURCE_DIR}/resources")
        target_compile_definitions(simpilot_${suite} PRIVATE
            UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
        target_compile_options(simpilot_${suite} PRIVATE /W4 /permissive- /utf-8 /EHsc)
        target_link_libraries(simpilot_${suite} PRIVATE simpilot_module_quick_launch)
        add_test(NAME simpilot_${suite} COMMAND simpilot_${suite})
        set_tests_properties(simpilot_${suite} PROPERTIES TIMEOUT 20)
    endforeach()
endif()
