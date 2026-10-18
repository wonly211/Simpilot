add_library(simpilot_module_windows_hotkey_blocker STATIC windows_hotkey_blocker_module.cpp)
target_include_directories(simpilot_module_windows_hotkey_blocker PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(simpilot_module_windows_hotkey_blocker PRIVATE
    UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(simpilot_module_windows_hotkey_blocker PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_module_windows_hotkey_blocker PUBLIC
    simpilot_module_support simpilot_win32_ui)
foreach(locale en-US zh-CN zh-TW)
    set_property(GLOBAL APPEND PROPERTY SIMPILOT_MODULE_LANGUAGE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Languages/${locale}.json")
endforeach()

if(BUILD_TESTING)
    add_executable(simpilot_windows_hotkey_policy_tests tests/windows_hotkey_policy_tests.cpp)
    target_include_directories(simpilot_windows_hotkey_policy_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_sources(simpilot_windows_hotkey_policy_tests PRIVATE "${PROJECT_BINARY_DIR}/BuiltinLanguagePack.rc")
    add_dependencies(simpilot_windows_hotkey_policy_tests simpilot_builtin_language_pack)
    target_compile_definitions(simpilot_windows_hotkey_policy_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_compile_options(simpilot_windows_hotkey_policy_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_windows_hotkey_policy_tests PRIVATE simpilot_module_windows_hotkey_blocker)
    add_test(NAME simpilot_windows_hotkey_policy_tests COMMAND simpilot_windows_hotkey_policy_tests)
endif()
