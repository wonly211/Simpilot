add_library(simpilot_module_mouse_locator STATIC
    cursor_locator.cpp mouse_shake_detector.cpp mouse_locator_module.cpp)
target_include_directories(simpilot_module_mouse_locator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(simpilot_module_mouse_locator PRIVATE
    UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(simpilot_module_mouse_locator PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_module_mouse_locator PUBLIC simpilot_module_support
    gdi32 user32 shcore)
foreach(locale en-US zh-CN zh-TW)
    set_property(GLOBAL APPEND PROPERTY SIMPILOT_MODULE_LANGUAGE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Languages/${locale}.json")
endforeach()

if(BUILD_TESTING)
    add_executable(simpilot_mouse_locator_tests tests/mouse_locator_tests.cpp)
    target_sources(simpilot_mouse_locator_tests PRIVATE "${PROJECT_BINARY_DIR}/BuiltinLanguagePack.rc")
    add_dependencies(simpilot_mouse_locator_tests simpilot_builtin_language_pack)
    target_compile_definitions(simpilot_mouse_locator_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_compile_options(simpilot_mouse_locator_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_mouse_locator_tests PRIVATE simpilot_module_mouse_locator)
    add_test(NAME simpilot_mouse_locator_tests COMMAND simpilot_mouse_locator_tests)
endif()
