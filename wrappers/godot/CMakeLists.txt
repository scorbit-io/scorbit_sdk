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

# The SDK stays its own shared library, shipped beside the extension. The extension loads it at run
# time from a copy in the game's user folder, which the SDK's updater can replace (src/sdk_loader).
# This local build still can't update itself: it is not a production build and has no ABI tag, so
# the SDK's updater refuses it.
CPMAddPackage(NAME scorbit_sdk SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_library(
    scorbit_godot
    SHARED
    src/register_types.cpp
    src/scorbit_game_state.cpp
    src/key_store.cpp
    src/sdk_loader.cpp
)
target_compile_features(scorbit_godot PRIVATE cxx_std_17)
# The SDK's headers only: its library is loaded at run time, never linked.
target_link_libraries(scorbit_godot PRIVATE godot-cpp ${CMAKE_DL_LIBS})
target_include_directories(
    scorbit_godot
    PRIVATE $<TARGET_PROPERTY:scorbit_sdk,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(
    scorbit_godot
    PRIVATE $<TARGET_PROPERTY:scorbit_sdk,INTERFACE_COMPILE_DEFINITIONS>
)
add_dependencies(scorbit_godot scorbit_sdk)

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

# Stage the add-on: the GDScript side from source, the two native libraries built here. A target
# of its own that runs on every build, so edits to the GDScript side are always picked up.
set(SCORBIT_GODOT_ADDON_DIR ${CMAKE_BINARY_DIR}/addons/scorbit)
if(WIN32)
    # A DLL has no soname.
    set(_sdk_runtime $<TARGET_FILE:scorbit_sdk>)
    set(_sdk_file_name $<TARGET_FILE_NAME:scorbit_sdk>)
else()
    set(_sdk_runtime $<TARGET_SONAME_FILE:scorbit_sdk>)
    set(_sdk_file_name $<TARGET_SONAME_FILE_NAME:scorbit_sdk>)
endif()
# The name the SDK is shipped under, beside the extension and in the game's user folder.
target_compile_definitions(scorbit_godot PRIVATE "SCORBIT_GODOT_SDK_FILE=\"${_sdk_file_name}\"")
add_custom_target(
    scorbit_godot_addon
    ALL
    COMMAND
        ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/addons/scorbit
        ${SCORBIT_GODOT_ADDON_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SCORBIT_GODOT_ADDON_DIR}/bin
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:scorbit_godot>
        ${SCORBIT_GODOT_ADDON_DIR}/bin/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_sdk_runtime} ${SCORBIT_GODOT_ADDON_DIR}/bin/
    COMMENT "Staging the Scorbit add-on in ${SCORBIT_GODOT_ADDON_DIR}"
    VERBATIM
)
add_dependencies(scorbit_godot_addon scorbit_godot scorbit_sdk)

# The device key store on its own, without Godot, so it runs in every build.
enable_testing()
add_executable(test_key_store test/test_key_store.cpp src/key_store.cpp)
target_compile_features(test_key_store PRIVATE cxx_std_17)
add_test(NAME key_store COMMAND test_key_store)

# A headless run of the add-on, wherever a Godot 4 binary is found (GODOT_EXECUTABLE overrides).
# Debug builds only: the editor and `godot -s` load the template_debug library.
find_program(GODOT_EXECUTABLE NAMES godot godot4 Godot)
if(GODOT_EXECUTABLE AND SCORBIT_GODOT_TARGET STREQUAL "template_debug")
    # The same session with the native library, and without it: every call must then do nothing.
    # With the native library, once with no SDK copy in the user folder, and once with a broken one.
    foreach(_case native native_corrupt_copy without_native)
        set(_without OFF)
        set(_copy fresh)
        if(_case STREQUAL "without_native")
            set(_without ON)
        elseif(_case STREQUAL "native_corrupt_copy")
            set(_copy corrupt)
        endif()
        add_test(
            NAME godot_smoke_${_case}
            COMMAND
                ${CMAKE_COMMAND} -DGODOT=${GODOT_EXECUTABLE}
                -DPROJECT_DIR=${CMAKE_BINARY_DIR}/test_project_${_case}
                -DADDON_DIR=${SCORBIT_GODOT_ADDON_DIR} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test
                -DWITHOUT_NATIVE=${_without} -DSDK_COPY=${_copy} -DCASE=${_case} -P
                ${CMAKE_CURRENT_SOURCE_DIR}/test/run_smoke.cmake
        )
    endforeach()
endif()
