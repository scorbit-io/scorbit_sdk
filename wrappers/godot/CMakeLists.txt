# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# The Godot 4 add-on: a GDExtension over the SDK's C API. Configured on its own, like tests/:
#   cmake -S wrappers/godot -B build/godot -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/godot
# The built add-on is staged in <build>/addons/scorbit, ready to copy into a Godot project.

cmake_minimum_required(VERSION 3.28)

project(scorbit_godot LANGUAGES C CXX)

set(MY_CMAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake)
include(${MY_CMAKE_DIR}/CPM.cmake)

# The oldest Godot the add-on loads in; later 4.x minors load it too, earlier ones refuse it.
# Keep in step with compatibility_minimum in addons/scorbit/scorbit.gdextension.
set(SCORBIT_GODOT_API_VERSION "4.3" CACHE STRING "Godot API version the add-on targets")
set(SCORBIT_GODOT_TARGET "template_release" CACHE STRING "template_debug or template_release")
set_property(CACHE SCORBIT_GODOT_TARGET PROPERTY STRINGS template_debug template_release)

# The SDK and godot-cpp link static dependencies into shared libraries.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

CPMAddPackage(
    NAME godot-cpp
    GITHUB_REPOSITORY godotengine/godot-cpp
    GIT_TAG 10.0.0-stable
    GIT_SHALLOW TRUE
    OPTIONS
        "GODOTCPP_API_VERSION ${SCORBIT_GODOT_API_VERSION}"
        "GODOTCPP_TARGET ${SCORBIT_GODOT_TARGET}"
)

# The SDK stays its own shared library beside the extension, so it can update itself in place as
# on every other host (SB-6222).
CPMAddPackage(NAME scorbit_sdk SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_library(scorbit_godot SHARED src/register_types.cpp src/scorbit_game_state.cpp)
target_compile_features(scorbit_godot PRIVATE cxx_std_17)
target_link_libraries(scorbit_godot PRIVATE godot-cpp scorbit_sdk::scorbit_sdk)

# No absolute paths in the binaries (godot-cpp's error macros embed __FILE__), as for the SDK.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(_target godot-cpp scorbit_godot)
        target_compile_options(
            ${_target}
            PRIVATE
                "-ffile-prefix-map=${godot-cpp_SOURCE_DIR}=godot-cpp"
                "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
                "-ffile-prefix-map=${CMAKE_BINARY_DIR}=."
                "-fdebug-prefix-map=${godot-cpp_SOURCE_DIR}=godot-cpp"
                "-fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
                "-fdebug-prefix-map=${CMAKE_BINARY_DIR}=."
        )
    endforeach()
endif()

# Godot's naming, e.g. libscorbit_godot.macos.template_release.dylib.
if(APPLE)
    set(_platform macos)
    set(_arch "")
elseif(WIN32)
    set(_platform windows)
    set(_arch ".x86_64")
else()
    set(_platform linux)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_arch ".arm64")
    else()
        set(_arch ".x86_64")
    endif()
endif()
set_target_properties(
    scorbit_godot
    PROPERTIES
        OUTPUT_NAME "scorbit_godot.${_platform}.${SCORBIT_GODOT_TARGET}${_arch}"
        CXX_VISIBILITY_PRESET hidden
)

# Find the SDK beside the extension in the editor, and where an export puts it: next to the game
# on Windows and Linux, in Contents/Frameworks on macOS.
# Only these: CMake's default build-tree rpath would put an absolute local path into the library.
if(APPLE)
    set(_rpath "@loader_path;@executable_path/../Frameworks")
else()
    set(_rpath "$ORIGIN")
endif()
set_target_properties(
    scorbit_godot
    PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "${_rpath}"
)

# Stage the add-on: the GDScript side from source, the two native libraries built here.
set(SCORBIT_GODOT_ADDON_DIR ${CMAKE_BINARY_DIR}/addons/scorbit)
add_custom_command(
    TARGET scorbit_godot
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/addons/scorbit
        ${SCORBIT_GODOT_ADDON_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SCORBIT_GODOT_ADDON_DIR}/bin
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:scorbit_godot>
        ${SCORBIT_GODOT_ADDON_DIR}/bin/
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different $<TARGET_SONAME_FILE:scorbit_sdk>
        ${SCORBIT_GODOT_ADDON_DIR}/bin/
)

# A headless run of the add-on, wherever a Godot 4 binary is found (GODOT_EXECUTABLE overrides).
find_program(GODOT_EXECUTABLE NAMES godot godot4 Godot)
if(GODOT_EXECUTABLE)
    enable_testing()
    # The same session with the native library, and without it: every call must then do nothing.
    foreach(_case native without_native)
        set(_without OFF)
        if(_case STREQUAL "without_native")
            set(_without ON)
        endif()
        add_test(
            NAME godot_smoke_${_case}
            COMMAND
                ${CMAKE_COMMAND} -DGODOT=${GODOT_EXECUTABLE}
                -DPROJECT_DIR=${CMAKE_BINARY_DIR}/test_project_${_case}
                -DADDON_DIR=${SCORBIT_GODOT_ADDON_DIR} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test
                -DWITHOUT_NATIVE=${_without} -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_smoke.cmake
        )
    endforeach()
endif()
