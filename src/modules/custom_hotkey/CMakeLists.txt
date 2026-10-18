add_library(simpilot_module_custom_hotkey STATIC
    custom_hotkey_module.cpp custom_hotkey_settings.cpp
    custom_hotkey_page.cpp custom_hotkey_dialog.cpp)
target_include_directories(simpilot_module_custom_hotkey PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(simpilot_module_custom_hotkey PRIVATE "${PROJECT_SOURCE_DIR}/resources")
target_compile_definitions(simpilot_module_custom_hotkey PRIVATE
    UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(simpilot_module_custom_hotkey PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_module_custom_hotkey PUBLIC
    simpilot_module_support simpilot_keyboard simpilot_hotkey_ui simpilot_command_execution comdlg32)
foreach(locale en-US zh-CN zh-TW)
    set_property(GLOBAL APPEND PROPERTY SIMPILOT_MODULE_LANGUAGE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Languages/${locale}.json")
endforeach()
if(BUILD_TESTING)
    add_executable(simpilot_custom_hotkey_tests tests/custom_hotkey_tests.cpp
        "${PROJECT_BINARY_DIR}/BuiltinLanguagePack.rc")
    add_dependencies(simpilot_custom_hotkey_tests simpilot_builtin_language_pack)
    target_compile_definitions(simpilot_custom_hotkey_tests PRIVATE
        UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_custom_hotkey_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_custom_hotkey_tests PRIVATE simpilot_module_custom_hotkey)
    add_test(NAME simpilot_custom_hotkey_tests COMMAND simpilot_custom_hotkey_tests)
endif()
