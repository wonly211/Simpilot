cmake_minimum_required(VERSION 3.24)

project(Simpilot VERSION 1.0.9 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Simpilot targets Windows 10/11.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimize across static module boundaries in the shipped configuration.
include(CheckIPOSupported)
check_ipo_supported(RESULT SIMPILOT_IPO_SUPPORTED OUTPUT SIMPILOT_IPO_ERROR LANGUAGES CXX)
if(NOT SIMPILOT_IPO_SUPPORTED)
    message(FATAL_ERROR "Release interprocedural optimization is unavailable: ${SIMPILOT_IPO_ERROR}")
endif()
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
add_compile_options("$<$<CONFIG:Release>:/Os>")

add_library(simpilot_core STATIC
    src/core/app_settings.cpp
    src/core/atomic_file.cpp
    src/core/command.cpp
    src/core/config_file.cpp
    src/core/hotkey.cpp
    src/core/logger.cpp
    src/core/localization.cpp
    src/core/settings_document.cpp
    src/core/text_encoding.cpp
    src/core/variable_expander.cpp
)
target_include_directories(simpilot_core PUBLIC include)
target_include_directories(simpilot_core PRIVATE resources)
target_include_directories(simpilot_core SYSTEM PRIVATE third_party)
target_compile_definitions(simpilot_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(simpilot_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_core PUBLIC advapi32 Cabinet shell32 ole32 user32 version)

add_library(simpilot_module_support STATIC
    src/app/module_registry.cpp
    src/app/settings_session.cpp
    src/app/ui_dispatcher.cpp
)
target_include_directories(simpilot_module_support PUBLIC include)
target_compile_definitions(simpilot_module_support PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(simpilot_module_support PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_module_support PUBLIC simpilot_core user32)

include(CTest)
add_library(simpilot_win32_ui STATIC
    src/infrastructure/ui/scrollable_form.cpp
    src/infrastructure/ui/settings_visual_style.cpp
    src/infrastructure/ui/toggle_switch.cpp)
target_include_directories(simpilot_win32_ui PUBLIC src/infrastructure/ui)
target_compile_definitions(simpilot_win32_ui PRIVATE
    UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(simpilot_win32_ui PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_win32_ui PUBLIC simpilot_core comctl32 gdi32 user32)

add_library(simpilot_command_execution STATIC src/infrastructure/command_executor.cpp)
target_compile_definitions(simpilot_command_execution PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(simpilot_command_execution PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_command_execution PUBLIC simpilot_core)

add_library(simpilot_hotkey_ui STATIC src/infrastructure/ui/hotkey_settings_page.cpp)
target_compile_definitions(simpilot_hotkey_ui PRIVATE
    UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(simpilot_hotkey_ui PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_hotkey_ui PUBLIC simpilot_keyboard simpilot_win32_ui simpilot_module_support)

option(SIMPILOT_MODULE_EVERYTHING "Build the Everything integration" ON)
if(SIMPILOT_MODULE_EVERYTHING)
    add_subdirectory(src/modules/everything)
endif()
option(SIMPILOT_MODULE_MOUSE_LOCATOR "Build the mouse locator" ON)
if(SIMPILOT_MODULE_MOUSE_LOCATOR)
    add_subdirectory(src/modules/mouse_locator)
endif()
option(SIMPILOT_MODULE_WINDOWS_HOTKEY_BLOCKER "Build Windows shortcut blocking policy" ON)
if(SIMPILOT_MODULE_WINDOWS_HOTKEY_BLOCKER)
    add_subdirectory(src/modules/windows_hotkey_blocker)
endif()
option(SIMPILOT_MODULE_KEYBOARD_MAPPING "Build physical keyboard mappings" ON)
if(SIMPILOT_MODULE_KEYBOARD_MAPPING)
    add_subdirectory(src/modules/keyboard_mapping)
endif()
option(SIMPILOT_MODULE_CUSTOM_HOTKEY "Build custom launch hotkeys" ON)
if(SIMPILOT_MODULE_CUSTOM_HOTKEY)
    add_subdirectory(src/modules/custom_hotkey)
endif()

option(SIMPILOT_MODULE_QUICK_LAUNCH "Build quick launch menus and editor" ON)
if(SIMPILOT_MODULE_QUICK_LAUNCH)
    add_subdirectory(src/modules/quick_launch)
endif()
option(SIMPILOT_MODULE_INPUT_METHOD "Build application input method rules" ON)
if(SIMPILOT_MODULE_INPUT_METHOD)
    add_subdirectory(src/modules/input_method)
endif()

add_executable(simpilot_language_pack_builder EXCLUDE_FROM_ALL
    tools/language_pack_builder.cpp
)
target_include_directories(simpilot_language_pack_builder PRIVATE include third_party)
target_compile_definitions(simpilot_language_pack_builder PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(simpilot_language_pack_builder PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_language_pack_builder PRIVATE Cabinet)

set(SIMPILOT_BUILTIN_LANGUAGE_PACK
    "${CMAKE_CURRENT_BINARY_DIR}/builtin-languages.lng")
get_property(SIMPILOT_MODULE_LANGUAGE_FILES GLOBAL PROPERTY SIMPILOT_MODULE_LANGUAGE_FILES)
get_property(SIMPILOT_MODULE_RUNTIME_FILES GLOBAL PROPERTY SIMPILOT_MODULE_RUNTIME_FILES)
string(JOIN "\n" SIMPILOT_MODULE_RUNTIME_MANIFEST ${SIMPILOT_MODULE_RUNTIME_FILES})
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/module-runtime-files.txt"
    CONTENT "${SIMPILOT_MODULE_RUNTIME_MANIFEST}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/module-language-inputs.txt"
    CONTENT "${SIMPILOT_MODULE_LANGUAGE_FILES}")
add_custom_command(
    OUTPUT "${SIMPILOT_BUILTIN_LANGUAGE_PACK}"
    COMMAND "$<TARGET_FILE:simpilot_language_pack_builder>"
            "${SIMPILOT_BUILTIN_LANGUAGE_PACK}"
            "${CMAKE_CURRENT_SOURCE_DIR}/Languages/en-US.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/Languages/zh-CN.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/Languages/zh-TW.json"
            ${SIMPILOT_MODULE_LANGUAGE_FILES}
    DEPENDS simpilot_language_pack_builder
            ${CMAKE_CURRENT_SOURCE_DIR}/Languages/en-US.json
            ${CMAKE_CURRENT_SOURCE_DIR}/Languages/zh-CN.json
            ${CMAKE_CURRENT_SOURCE_DIR}/Languages/zh-TW.json
            ${SIMPILOT_MODULE_LANGUAGE_FILES}
            "${CMAKE_CURRENT_BINARY_DIR}/module-language-inputs.txt"
    COMMENT "Packing built-in language resources")
add_custom_target(simpilot_builtin_language_pack
    DEPENDS "${SIMPILOT_BUILTIN_LANGUAGE_PACK}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/BuiltinLanguagePack.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BuiltinLanguagePack.rc"
    @ONLY)

add_library(simpilot_keyboard STATIC
    src/infrastructure/keyboard/keyboard_manager.cpp
    src/infrastructure/keyboard/keyboard_capture_state.cpp
    src/infrastructure/keyboard/keyboard_input.cpp
)
target_include_directories(simpilot_keyboard PUBLIC
    include
    src/infrastructure/keyboard
)
target_compile_definitions(simpilot_keyboard PRIVATE
    UNICODE _UNICODE NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
)
target_compile_options(simpilot_keyboard PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_keyboard PUBLIC simpilot_core user32)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/Simpilot.manifest.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Simpilot.manifest"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/SimpilotVersion.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SimpilotVersion.rc"
    @ONLY
)

add_executable(simpilot WIN32
    resources/Simpilot.rc
    "${CMAKE_CURRENT_BINARY_DIR}/BuiltinLanguagePack.rc"
    "${CMAKE_CURRENT_BINARY_DIR}/SimpilotVersion.rc"
    src/app/about_window.cpp
    src/app/general_settings_page.cpp
    src/app/main.cpp
    src/app/register_builtin_modules.cpp
    src/app/settings_window.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/Simpilot.manifest"
    src/app/tray_application.cpp
)
add_dependencies(simpilot simpilot_builtin_language_pack)
target_include_directories(simpilot PRIVATE
    include
    resources
    src/app
)
target_compile_definitions(simpilot PRIVATE
    UNICODE _UNICODE NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
    SIMPILOT_VERSION=L\"${PROJECT_VERSION}\"
    SIMPILOT_MODULE_EVERYTHING=$<BOOL:${SIMPILOT_MODULE_EVERYTHING}>
    SIMPILOT_MODULE_MOUSE_LOCATOR=$<BOOL:${SIMPILOT_MODULE_MOUSE_LOCATOR}>
    SIMPILOT_MODULE_WINDOWS_HOTKEY_BLOCKER=$<BOOL:${SIMPILOT_MODULE_WINDOWS_HOTKEY_BLOCKER}>
    SIMPILOT_MODULE_KEYBOARD_MAPPING=$<BOOL:${SIMPILOT_MODULE_KEYBOARD_MAPPING}>
    SIMPILOT_MODULE_CUSTOM_HOTKEY=$<BOOL:${SIMPILOT_MODULE_CUSTOM_HOTKEY}>
    SIMPILOT_MODULE_QUICK_LAUNCH=$<BOOL:${SIMPILOT_MODULE_QUICK_LAUNCH}>
    SIMPILOT_MODULE_INPUT_METHOD=$<BOOL:${SIMPILOT_MODULE_INPUT_METHOD}>
)
target_compile_options(simpilot PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot PRIVATE
    simpilot_keyboard simpilot_module_support simpilot_win32_ui
    simpilot_hotkey_ui simpilot_command_execution comctl32 comdlg32 shell32 shcore user32)
if(SIMPILOT_MODULE_EVERYTHING)
    target_link_libraries(simpilot PRIVATE simpilot_module_everything)
    simpilot_package_everything(simpilot)
endif()
if(SIMPILOT_MODULE_MOUSE_LOCATOR)
    target_link_libraries(simpilot PRIVATE simpilot_module_mouse_locator)
endif()
if(SIMPILOT_MODULE_WINDOWS_HOTKEY_BLOCKER)
    target_link_libraries(simpilot PRIVATE simpilot_module_windows_hotkey_blocker)
endif()
if(SIMPILOT_MODULE_KEYBOARD_MAPPING)
    target_link_libraries(simpilot PRIVATE simpilot_module_keyboard_mapping)
endif()
if(SIMPILOT_MODULE_CUSTOM_HOTKEY)
    target_link_libraries(simpilot PRIVATE simpilot_module_custom_hotkey)
endif()
if(SIMPILOT_MODULE_QUICK_LAUNCH)
    target_link_libraries(simpilot PRIVATE simpilot_module_quick_launch)
endif()
if(SIMPILOT_MODULE_INPUT_METHOD)
    target_link_libraries(simpilot PRIVATE simpilot_module_input_method)
endif()
set_target_properties(simpilot PROPERTIES OUTPUT_NAME "Simpilot")

set(SIMPILOT_LANGUAGE_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Languages")
set(SIMPILOT_LANGUAGE_RESOURCE_FILES
    en-US.json
    zh-CN.json
    zh-TW.json
)
foreach(resource IN LISTS SIMPILOT_LANGUAGE_RESOURCE_FILES)
    if(NOT EXISTS "${SIMPILOT_LANGUAGE_RESOURCE_DIR}/${resource}")
        message(FATAL_ERROR "Missing language resource: ${resource}")
    endif()
endforeach()
if(BUILD_TESTING)
    find_program(SIMPILOT_POWERSHELL NAMES pwsh powershell REQUIRED)
    add_test(NAME simpilot_module_boundaries
        COMMAND "${SIMPILOT_POWERSHELL}" -NoProfile -File
            "${PROJECT_SOURCE_DIR}/tools/check-module-boundaries.ps1")
    add_test(NAME simpilot_release_size_policy_tests
        COMMAND "${SIMPILOT_POWERSHELL}" -NoProfile -File
            "${PROJECT_SOURCE_DIR}/tests/release_size_policy_tests.ps1")
    add_executable(simpilot_module_support_tests tests/module_support_tests.cpp)
    target_compile_definitions(simpilot_module_support_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(simpilot_module_support_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_module_support_tests PRIVATE simpilot_module_support)
    add_test(NAME simpilot_module_support_tests COMMAND simpilot_module_support_tests)
    add_executable(simpilot_visual_style_tests tests/visual_style_tests.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/Simpilot.manifest")
    target_compile_definitions(simpilot_visual_style_tests PRIVATE
        UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_visual_style_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_visual_style_tests PRIVATE simpilot_win32_ui)
    add_test(NAME simpilot_visual_style_tests COMMAND simpilot_visual_style_tests)
    add_executable(simpilot_settings_module_tests
        resources/Simpilot.rc
        "${CMAKE_CURRENT_BINARY_DIR}/BuiltinLanguagePack.rc"
        tests/settings_module_integration_tests.cpp
        src/app/general_settings_page.cpp
        src/app/settings_window.cpp
)
    target_include_directories(simpilot_settings_module_tests PRIVATE include src/app resources)
    target_compile_definitions(simpilot_settings_module_tests PRIVATE
        UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_settings_module_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_settings_module_tests PRIVATE
        simpilot_keyboard simpilot_module_support simpilot_win32_ui simpilot_hotkey_ui comctl32 comdlg32 gdi32 shcore)
    add_dependencies(simpilot_settings_module_tests simpilot_builtin_language_pack)
    add_test(NAME simpilot_settings_module_tests COMMAND simpilot_settings_module_tests)
    set_tests_properties(simpilot_settings_module_tests PROPERTIES TIMEOUT 15)
    add_executable(simpilot_core_tests
        "${CMAKE_CURRENT_BINARY_DIR}/BuiltinLanguagePack.rc"
        tests/core_tests.cpp)
    target_include_directories(simpilot_core_tests PRIVATE include)
    target_compile_definitions(simpilot_core_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(simpilot_core_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_core_tests PRIVATE simpilot_core)
    add_dependencies(simpilot_core_tests simpilot)
    add_test(NAME simpilot_core_tests COMMAND simpilot_core_tests)

    add_executable(simpilot_localization_resource_tests
        "${CMAKE_CURRENT_BINARY_DIR}/BuiltinLanguagePack.rc"
        tests/localization_resource_tests.cpp
    )
    target_include_directories(simpilot_localization_resource_tests PRIVATE include third_party resources)
    target_compile_definitions(simpilot_localization_resource_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(simpilot_localization_resource_tests PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_localization_resource_tests PRIVATE simpilot_core)
    add_dependencies(simpilot_localization_resource_tests simpilot)
    add_test(NAME simpilot_localization_resource_tests
             COMMAND simpilot_localization_resource_tests)

    add_executable(simpilot_windows_hotkey_blocker_tests
        tests/windows_hotkey_blocker_tests.cpp
    )
    target_compile_definitions(simpilot_windows_hotkey_blocker_tests PRIVATE
        UNICODE _UNICODE NOMINMAX)
    target_compile_options(simpilot_windows_hotkey_blocker_tests PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_windows_hotkey_blocker_tests PRIVATE simpilot_keyboard)
    add_test(NAME simpilot_windows_hotkey_blocker_tests
             COMMAND simpilot_windows_hotkey_blocker_tests)

    add_executable(simpilot_keyboard_thread_lifecycle_test
        tests/keyboard_thread_lifecycle_test.cpp
    )
    target_compile_definitions(simpilot_keyboard_thread_lifecycle_test PRIVATE
        UNICODE _UNICODE NOMINMAX
        WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_keyboard_thread_lifecycle_test PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_keyboard_thread_lifecycle_test PRIVATE simpilot_keyboard)
    add_test(NAME simpilot_keyboard_thread_lifecycle_test
             COMMAND simpilot_keyboard_thread_lifecycle_test)

    add_executable(simpilot_keyboard_capture_state_tests
        tests/keyboard_capture_state_tests.cpp
    )
    target_include_directories(simpilot_keyboard_capture_state_tests PRIVATE
        include
        src/app
    )
    target_compile_definitions(simpilot_keyboard_capture_state_tests PRIVATE
        UNICODE _UNICODE NOMINMAX
        WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_keyboard_capture_state_tests PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_keyboard_capture_state_tests PRIVATE
        simpilot_keyboard)
    add_test(NAME simpilot_keyboard_capture_state_tests
             COMMAND simpilot_keyboard_capture_state_tests)

    add_executable(simpilot_about_window_test
        resources/Simpilot.rc
        "${CMAKE_CURRENT_BINARY_DIR}/BuiltinLanguagePack.rc"
        "${CMAKE_CURRENT_BINARY_DIR}/Simpilot.manifest"
        tests/about_window_test.cpp
        src/app/about_window.cpp
    )
    target_include_directories(simpilot_about_window_test PRIVATE
        include
        resources
        src/app
    )
    target_compile_definitions(simpilot_about_window_test PRIVATE
        UNICODE _UNICODE NOMINMAX
        WINVER=0x0A00 _WIN32_WINNT=0x0A00
        SIMPILOT_VERSION=L\"${PROJECT_VERSION}\")
    target_compile_options(simpilot_about_window_test PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_about_window_test PRIVATE
        simpilot_core simpilot_win32_ui comctl32 gdi32 shell32 shcore user32 version)
    add_dependencies(simpilot_about_window_test simpilot)
    add_test(NAME simpilot_about_window_test COMMAND simpilot_about_window_test)
    set_tests_properties(simpilot_about_window_test PROPERTIES TIMEOUT 10)

endif()

install(TARGETS simpilot RUNTIME DESTINATION .)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-NOTICES.txt"
    DESTINATION .
)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "Simpilot")
set(CPACK_PACKAGE_VENDOR "Simpilot")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "Simpilot-${PROJECT_VERSION}-win-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_PACKAGE_CHECKSUM "SHA256")
include(CPack)
