cmake_minimum_required(VERSION 3.24)

project(Simpilot VERSION 0.17.1 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Simpilot targets Windows 10/11.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(simpilot_core STATIC
    src/core/app_settings.cpp
    src/core/command.cpp
    src/core/config_file.cpp
    src/core/config_watcher.cpp
    src/core/everything.cpp
    src/core/hotkey.cpp
    src/core/logger.cpp
    src/core/localization.cpp
    src/core/menu_parser.cpp
    src/core/menu_writer.cpp
    src/core/program_cache.cpp
    src/core/program_resolver.cpp
    src/core/text_encoding.cpp
    src/core/variable_expander.cpp
)
target_include_directories(simpilot_core PUBLIC include)
target_include_directories(simpilot_core SYSTEM PRIVATE third_party)
target_compile_definitions(simpilot_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(simpilot_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_core PUBLIC advapi32 shell32 ole32 user32 version)

add_library(simpilot_keyboard STATIC
    src/app/keyboard_manager.cpp
    third_party/PowerToys/KeyboardManager/KeyboardManagerState.cpp
    third_party/PowerToys/KeyboardManager/Shortcut.cpp
)
target_include_directories(simpilot_keyboard PUBLIC
    include
    src/app
    third_party/PowerToys/KeyboardManager
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
    "${CMAKE_CURRENT_BINARY_DIR}/SimpilotVersion.rc"
    src/app/about_window.cpp
    src/app/custom_hotkey_dialog.cpp
    src/app/launch_menu_renderer.cpp
    src/app/main.cpp
    src/app/menu_editor_window.cpp
    src/app/menu_icon_cache.cpp
    src/app/menu_theme.cpp
    src/app/program_selection_dialog.cpp
    src/app/settings_window.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/Simpilot.manifest"
    src/app/toggle_switch.cpp
    src/app/tray_application.cpp
)
target_include_directories(simpilot PRIVATE
    include
    resources
    src/app
)
target_compile_definitions(simpilot PRIVATE
    UNICODE _UNICODE NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
    SIMPILOT_VERSION=L\"${PROJECT_VERSION}\"
)
target_compile_options(simpilot PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot PRIVATE
    simpilot_keyboard comctl32 comdlg32 shell32 shcore user32)
set_target_properties(simpilot PROPERTIES OUTPUT_NAME "Simpilot")

set(EVERYTHING_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Everything")
set(EVERYTHING_COMPONENTS
    Everything.exe
    Everything64.dll
    Everything.lng
    Everything.ini
)
foreach(component IN LISTS EVERYTHING_COMPONENTS)
    if(NOT EXISTS "${EVERYTHING_SOURCE_DIR}/${component}")
        message(FATAL_ERROR "Missing Everything release component: ${component}")
    endif()
endforeach()

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
list(TRANSFORM SIMPILOT_LANGUAGE_RESOURCE_FILES
     PREPEND "${SIMPILOT_LANGUAGE_RESOURCE_DIR}/"
     OUTPUT_VARIABLE SIMPILOT_LANGUAGE_RESOURCE_PATHS)
list(TRANSFORM EVERYTHING_COMPONENTS
     PREPEND "${EVERYTHING_SOURCE_DIR}/"
     OUTPUT_VARIABLE EVERYTHING_COMPONENT_PATHS)

add_custom_command(TARGET simpilot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:simpilot>/Everything"
    COMMENT "Preparing portable Everything components"
)
foreach(component IN LISTS EVERYTHING_COMPONENTS)
    add_custom_command(TARGET simpilot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EVERYTHING_SOURCE_DIR}/${component}"
                "$<TARGET_FILE_DIR:simpilot>/Everything/${component}"
    )
endforeach()
add_custom_command(TARGET simpilot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:simpilot>/Languages"
    COMMENT "Preparing JSON language resources"
)
foreach(resource IN LISTS SIMPILOT_LANGUAGE_RESOURCE_FILES)
    add_custom_command(TARGET simpilot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SIMPILOT_LANGUAGE_RESOURCE_DIR}/${resource}"
                "$<TARGET_FILE_DIR:simpilot>/Languages/${resource}"
    )
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(simpilot_core_tests tests/core_tests.cpp)
    target_include_directories(simpilot_core_tests PRIVATE include)
    target_compile_definitions(simpilot_core_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(simpilot_core_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_core_tests PRIVATE simpilot_core)
    add_dependencies(simpilot_core_tests simpilot)
    add_test(NAME simpilot_core_tests COMMAND simpilot_core_tests)

    add_executable(simpilot_localization_resource_tests
        tests/localization_resource_tests.cpp
    )
    target_include_directories(simpilot_localization_resource_tests PRIVATE third_party)
    target_compile_definitions(simpilot_localization_resource_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(simpilot_localization_resource_tests PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
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

    add_executable(simpilot_menu_presentation_tests
        tests/menu_presentation_tests.cpp
        src/app/launch_menu_renderer.cpp
        src/app/menu_icon_cache.cpp
        src/app/menu_theme.cpp
    )
    target_include_directories(simpilot_menu_presentation_tests PRIVATE
        include
        src/app
    )
    target_compile_definitions(simpilot_menu_presentation_tests PRIVATE
        UNICODE _UNICODE NOMINMAX
        WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_menu_presentation_tests PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_menu_presentation_tests PRIVATE
        simpilot_core shell32 user32)
    add_test(NAME simpilot_menu_presentation_tests
             COMMAND simpilot_menu_presentation_tests)

    add_executable(simpilot_program_selection_dialog_test
        resources/Simpilot.rc
        tests/program_selection_dialog_test.cpp
        src/app/program_selection_dialog.cpp
    )
    target_include_directories(simpilot_program_selection_dialog_test PRIVATE
        include
        resources
        src/app
    )
    target_compile_definitions(simpilot_program_selection_dialog_test PRIVATE
        UNICODE _UNICODE NOMINMAX
        WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_program_selection_dialog_test PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_program_selection_dialog_test PRIVATE
        simpilot_core comctl32 shell32 user32)
    add_test(NAME simpilot_program_selection_dialog_test
             COMMAND simpilot_program_selection_dialog_test)
    set_tests_properties(simpilot_program_selection_dialog_test PROPERTIES TIMEOUT 10)

    add_executable(simpilot_about_window_test
        resources/Simpilot.rc
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
        WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(simpilot_about_window_test PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_about_window_test PRIVATE
        simpilot_core comctl32 gdi32 shell32 shcore user32 version)
    add_dependencies(simpilot_about_window_test simpilot)
    add_test(NAME simpilot_about_window_test COMMAND simpilot_about_window_test)
    set_tests_properties(simpilot_about_window_test PROPERTIES TIMEOUT 10)

endif()

install(TARGETS simpilot RUNTIME DESTINATION .)
install(FILES
    ${EVERYTHING_COMPONENT_PATHS}
    DESTINATION Everything
)
install(FILES
    ${SIMPILOT_LANGUAGE_RESOURCE_PATHS}
    DESTINATION Languages
)
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
