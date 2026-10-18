add_library(simpilot_module_keyboard_mapping STATIC
    keyboard_mapping.cpp keyboard_mapping_settings.cpp keyboard_mapping_engine.cpp
    keyboard_mapping_editor_model.cpp keyboard_mapping_dialog.cpp
    keyboard_mapping_page.cpp keyboard_mapping_module.cpp)
target_include_directories(simpilot_module_keyboard_mapping PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(simpilot_module_keyboard_mapping PRIVATE "${PROJECT_SOURCE_DIR}/resources")
target_compile_definitions(simpilot_module_keyboard_mapping PRIVATE
    UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(simpilot_module_keyboard_mapping PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_module_keyboard_mapping PUBLIC
    simpilot_module_support simpilot_keyboard simpilot_win32_ui PRIVATE comdlg32)
foreach(locale en-US zh-CN zh-TW)
    set_property(GLOBAL APPEND PROPERTY SIMPILOT_MODULE_LANGUAGE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Languages/${locale}.json")
endforeach()
if(BUILD_TESTING)
    foreach(suite engine settings module applications dialog)
        add_executable(simpilot_keyboard_mapping_${suite}_tests tests/keyboard_mapping_${suite}_tests.cpp)
        target_sources(simpilot_keyboard_mapping_${suite}_tests PRIVATE
            "${PROJECT_BINARY_DIR}/BuiltinLanguagePack.rc")
        add_dependencies(simpilot_keyboard_mapping_${suite}_tests simpilot_builtin_language_pack)
        target_compile_definitions(simpilot_keyboard_mapping_${suite}_tests PRIVATE
            UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
        target_compile_options(simpilot_keyboard_mapping_${suite}_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        target_link_libraries(simpilot_keyboard_mapping_${suite}_tests PRIVATE simpilot_module_keyboard_mapping)
        add_test(NAME simpilot_keyboard_mapping_${suite}_tests COMMAND simpilot_keyboard_mapping_${suite}_tests)
    endforeach()
    target_sources(simpilot_keyboard_mapping_dialog_tests PRIVATE "${PROJECT_BINARY_DIR}/Simpilot.manifest")
    set_tests_properties(simpilot_keyboard_mapping_dialog_tests PROPERTIES TIMEOUT 30)
endif()
