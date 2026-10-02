add_library(simpilot_module_input_method STATIC
    input_method_module.cpp input_method_model.cpp input_method_backend.cpp
    input_method_runtime.cpp input_method_page.cpp
    running_applications.cpp running_application_dialog.cpp)
target_include_directories(simpilot_module_input_method PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(simpilot_module_input_method PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(simpilot_module_input_method PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_module_input_method PUBLIC
    simpilot_module_support simpilot_win32_ui
    PRIVATE comdlg32 dwmapi imm32 uuid ole32 oleaut32 shell32 user32)

foreach(locale en-US zh-CN zh-TW)
    set_property(GLOBAL APPEND PROPERTY SIMPILOT_MODULE_LANGUAGE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Languages/${locale}.json")
endforeach()

if(BUILD_TESTING)
    add_executable(simpilot_input_method_tests tests/input_method_tests.cpp)
    target_include_directories(simpilot_input_method_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(simpilot_input_method_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_input_method_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_input_method_tests PRIVATE simpilot_module_input_method)
    add_test(NAME simpilot_input_method_tests COMMAND simpilot_input_method_tests)
    add_executable(simpilot_input_method_ui_tests tests/input_method_ui_tests.cpp
        "${PROJECT_BINARY_DIR}/BuiltinLanguagePack.rc"
        "${PROJECT_BINARY_DIR}/Simpilot.manifest")
    add_dependencies(simpilot_input_method_ui_tests simpilot_builtin_language_pack)
    target_include_directories(simpilot_input_method_ui_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(simpilot_input_method_ui_tests PRIVATE
        UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_input_method_ui_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_input_method_ui_tests PRIVATE simpilot_module_input_method)
    add_test(NAME simpilot_input_method_ui_tests COMMAND simpilot_input_method_ui_tests)
    set_tests_properties(simpilot_input_method_ui_tests PROPERTIES TIMEOUT 30)
    add_executable(simpilot_running_application_tests tests/running_application_tests.cpp
        "${PROJECT_BINARY_DIR}/BuiltinLanguagePack.rc"
        "${PROJECT_BINARY_DIR}/Simpilot.manifest")
    add_dependencies(simpilot_running_application_tests simpilot_builtin_language_pack)
    target_include_directories(simpilot_running_application_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(simpilot_running_application_tests PRIVATE
        UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_running_application_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_running_application_tests PRIVATE simpilot_module_input_method)
    add_test(NAME simpilot_running_application_tests COMMAND simpilot_running_application_tests)
    set_tests_properties(simpilot_running_application_tests PROPERTIES TIMEOUT 30)
    add_executable(simpilot_input_method_native_probe tests/input_method_native_probe.cpp
        "${PROJECT_BINARY_DIR}/BuiltinLanguagePack.rc"
        "${PROJECT_BINARY_DIR}/Simpilot.manifest")
    add_dependencies(simpilot_input_method_native_probe simpilot_builtin_language_pack)
    target_compile_definitions(simpilot_input_method_native_probe PRIVATE
        UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_input_method_native_probe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_input_method_native_probe PRIVATE
        simpilot_module_input_method windowscodecs)
endif()
