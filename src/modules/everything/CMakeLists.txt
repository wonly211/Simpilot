add_library(simpilot_module_everything STATIC everything.cpp everything_module.cpp)
target_include_directories(simpilot_module_everything PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(simpilot_module_everything PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(simpilot_module_everything PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(simpilot_module_everything PUBLIC simpilot_module_support)
foreach(locale en-US zh-CN zh-TW)
    set_property(GLOBAL APPEND PROPERTY SIMPILOT_MODULE_LANGUAGE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Languages/${locale}.json")
endforeach()

set(SIMPILOT_EVERYTHING_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/Everything")
set(SIMPILOT_EVERYTHING_COMPONENTS Everything.exe Everything64.dll Everything.lng Everything.ini)
foreach(component IN LISTS SIMPILOT_EVERYTHING_COMPONENTS)
    set_property(GLOBAL APPEND PROPERTY SIMPILOT_MODULE_RUNTIME_FILES "Everything/${component}")
    if(NOT EXISTS "${SIMPILOT_EVERYTHING_SOURCE_DIR}/${component}")
        message(FATAL_ERROR "Missing Everything release component: ${component}")
    endif()
    install(FILES "${SIMPILOT_EVERYTHING_SOURCE_DIR}/${component}" DESTINATION Everything)
endforeach()

# POST_BUILD commands must be declared in the executable's directory.
function(simpilot_package_everything target)
    set(source "${PROJECT_SOURCE_DIR}/third_party/Everything")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Everything"
        COMMENT "Preparing portable Everything components")
    foreach(component Everything.exe Everything64.dll Everything.lng Everything.ini)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${source}/${component}" "$<TARGET_FILE_DIR:${target}>/Everything/${component}")
    endforeach()
endfunction()

if(BUILD_TESTING)
    add_executable(simpilot_everything_tests tests/everything_tests.cpp)
    target_sources(simpilot_everything_tests PRIVATE "${PROJECT_BINARY_DIR}/BuiltinLanguagePack.rc")
    add_dependencies(simpilot_everything_tests simpilot_builtin_language_pack)
    target_compile_definitions(simpilot_everything_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        SIMPILOT_EVERYTHING_SDK=L\"${SIMPILOT_EVERYTHING_SOURCE_DIR}/Everything64.dll\")
    target_compile_options(simpilot_everything_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_libraries(simpilot_everything_tests PRIVATE simpilot_module_everything)
    add_test(NAME simpilot_everything_tests COMMAND simpilot_everything_tests)
endif()
