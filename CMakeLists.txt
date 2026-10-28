cmake_minimum_required(VERSION 3.25)
project(DirectKeycast VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static CRT: the exe runs on machines without the VC++ redistributable.
# Set before add_subdirectory so msimeui links the same runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(MSIMEUI_DIR "${CMAKE_SOURCE_DIR}/third_party/msimeui")
if(NOT EXISTS "${MSIMEUI_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "msimeui submodule is missing at ${MSIMEUI_DIR}. "
        "Run: git submodule update --init --recursive")
endif()
add_subdirectory("${MSIMEUI_DIR}" EXCLUDE_FROM_ALL)

configure_file(
    "${CMAKE_SOURCE_DIR}/includes/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)

set(SRC_FILES
    # headers
    "./includes/config.h"
    "./includes/CTimer.h"
    "./includes/d2d.h"
    "./includes/globals.h"
    "./includes/hook.h"
    "./includes/main.h"
    "./includes/msimeui_host.h"
    "./includes/resource.h"
    "./includes/settings.h"
    "./includes/settings_controls.h"
    "./includes/svg_icons.h"
    "./includes/theme.h"
    "./includes/tray_menu.h"
    "./includes/utils.h"
    "./includes/version.h.in"
    "./includes/window.h"
    # source files
    "./src/config.cpp"
    "./src/d2d.cpp"
    "./src/hook.cpp"
    "./src/main.cpp"
    "./src/msimeui_host.cpp"
    "./src/settings.cpp"
    "./src/settings_controls.cpp"
    "./src/svg_icons.cpp"
    "./src/theme.cpp"
    "./src/tray_menu.cpp"
    "./src/utils.cpp"
    "./src/window.cpp"
    # resources
    "./src/directkeycast.rc"
)

set(MY_EXECUTABLE_NAME "directkeycast")

add_executable(${MY_EXECUTABLE_NAME} WIN32
    ${SRC_FILES}
)

target_include_directories(${MY_EXECUTABLE_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/includes"
    "${CMAKE_BINARY_DIR}/generated"
)

target_compile_definitions(${MY_EXECUTABLE_NAME} PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
)

if(MSVC)
    # /utf-8: sources are BOM-less UTF-8 with Chinese literals; without it MSVC
    # decodes them with the system code page and the UI text turns into mojibake.
    target_compile_options(${MY_EXECUTABLE_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W3 /permissive- /MP>
    )
endif()

target_link_libraries(${MY_EXECUTABLE_NAME} PRIVATE msimeui)

set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${MY_EXECUTABLE_NAME})
set_property(TARGET ${MY_EXECUTABLE_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${MY_EXECUTABLE_NAME}>")

option(DIRECTKEYCAST_BUILD_TESTS "Build overlay layout tests" OFF)
if(DIRECTKEYCAST_BUILD_TESTS)
    enable_testing()
    add_executable(overlay_layout_tests
        tests/overlay_layout_tests.cpp
        src/d2d.cpp
        src/utils.cpp
        src/theme.cpp
    )
    target_include_directories(overlay_layout_tests PRIVATE includes)
    target_compile_definitions(overlay_layout_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        target_compile_options(overlay_layout_tests PRIVATE /utf-8)
    endif()
    target_link_libraries(overlay_layout_tests PRIVATE msimeui)
    add_test(NAME overlay_layout COMMAND overlay_layout_tests)
endif()
