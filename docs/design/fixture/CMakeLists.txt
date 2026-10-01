cmake_minimum_required(VERSION 3.24)
project(SimpilotDesignAudit LANGUAGES CXX RC)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This isolated fixture requires MSVC x64 and the existing Release libraries.")
endif()
get_filename_component(root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(built "${root}/build/ci-vs2022-x64")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(audit_preview WIN32 audit_preview.cpp
    "${root}/src/app/settings_window.cpp"
    "${root}/src/app/general_settings_page.cpp"
    "${root}/src/app/about_window.cpp"
    "${root}/resources/Simpilot.rc"
    "${built}/BuiltinLanguagePack.rc"
    "${built}/Simpilot.manifest")
target_include_directories(audit_preview PRIVATE
    "${root}/include" "${root}/resources" "${root}/src/app"
    "${root}/src/infrastructure" "${root}/src/infrastructure/ui"
    "${root}/src/infrastructure/keyboard")
target_compile_definitions(audit_preview PRIVATE UNICODE _UNICODE NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(audit_preview PRIVATE /utf-8 /EHsc /W4)

# Existing artifacts only: do not add_subdirectory() or rebuild production.
set(audit_inputs "${built}/BuiltinLanguagePack.rc" "${built}/builtin-languages.lng"
    "${built}/Simpilot.manifest")
set(SIMPILOT_AUDIT_MENU_RENDERER "" CACHE FILEPATH
    "Optional historical launch_menu_renderer.cpp for isolated visual comparison only")
if(SIMPILOT_AUDIT_MENU_RENDERER)
    if(NOT EXISTS "${SIMPILOT_AUDIT_MENU_RENDERER}")
        message(FATAL_ERROR "Historical renderer source does not exist")
    endif()
    # Executable definitions satisfy the references before the static library is searched.
    target_sources(audit_preview PRIVATE "${SIMPILOT_AUDIT_MENU_RENDERER}")
    list(APPEND audit_inputs "${SIMPILOT_AUDIT_MENU_RENDERER}")
endif()
foreach(module everything mouse_locator windows_hotkey_blocker custom_hotkey keyboard_mapping quick_launch)
    target_include_directories(audit_preview PRIVATE "${root}/src/modules/${module}")
    set(library "${built}/src/modules/${module}/Release/simpilot_module_${module}.lib")
    target_link_libraries(audit_preview PRIVATE "${library}")
    list(APPEND audit_inputs "${library}")
endforeach()
foreach(name core keyboard module_support win32_ui hotkey_ui)
    set(library "${built}/Release/simpilot_${name}.lib")
    target_link_libraries(audit_preview PRIVATE "${library}")
    list(APPEND audit_inputs "${library}")
endforeach()

# The fixture supplies execute_command/user_profile_directory. In addition, the
# delay-import hook denies ShellExecute[Ex]W used by About and menu-config links.
target_link_libraries(audit_preview PRIVATE comctl32 comdlg32 gdi32 shcore user32 windowscodecs
    advapi32 Cabinet shell32 ole32 version delayimp)
target_link_options(audit_preview PRIVATE /DELAYLOAD:shell32.dll)
string(TIMESTAMP audit_configured "%Y-%m-%dT%H:%M:%SZ" UTC)
target_compile_definitions(audit_preview PRIVATE AUDIT_CONFIGURED="${audit_configured}")
set(provenance "Configured UTC: ${audit_configured}\nSource: ${CMAKE_CURRENT_SOURCE_DIR}\n")
foreach(input IN LISTS audit_inputs)
    if(NOT EXISTS "${input}")
        message(FATAL_ERROR "Missing existing build input: ${input}")
    endif()
    file(SHA256 "${input}" digest)
    string(APPEND provenance "${digest}  ${input}\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/input-provenance.txt" CONTENT "${provenance}")
