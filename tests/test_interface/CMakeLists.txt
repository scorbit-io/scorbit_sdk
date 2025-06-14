# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

cmake_minimum_required(VERSION 3.28)

project(scorbit_sdk_tests LANGUAGES CXX)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../../cmake/tools.cmake)

# ---- Dependencies ----

include(../../cmake/CPM.cmake)

# Catch2
CPMAddPackage(
    NAME Catch2
    GIT_TAG v3.8.1
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_SHALLOW TRUE
    OPTIONS "CATCH_INSTALL_DOCS OFF" "CATCH_INSTALL_HELPERS OFF" "CATCH_INSTALL_TESTS OFF"
)

# Trompeloeil
CPMAddPackage(
    NAME trompeloeil
    GIT_TAG v49
    GITHUB_REPOSITORY rollbear/trompeloeil
    OPTIONS "TROMPELOEIL_INSTALL_DOCS OFF"
)

# scorbit_sdk
if(TEST_INSTALLED_VERSION)
    find_package(scorbit_sdk REQUIRED)
else()
    CPMAddPackage(NAME scorbit_sdk SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
endif()

# ---- Create binary ----

add_executable(${PROJECT_NAME})

target_sources(
    ${PROJECT_NAME}
    PRIVATE source/test_game_state_c.cpp source/test_version.cpp source/test_device_info.cpp
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE scorbit_sdk::scorbit_sdk Catch2::Catch2WithMain trompeloeil::trompeloeil fmt::fmt
)
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

# enable compiler warnings
if(NOT TEST_INSTALLED_VERSION)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(scorbit_sdk PUBLIC -Wall -Wpedantic -Wextra -Werror)
    elseif(MSVC)
        # target_compile_options(scorbit_sdk PUBLIC /W4 /WX)
    endif()
endif()

# In windows shared library has to be copied near to executable
if(WIN32 AND SCORBIT_SDK_SHARED)
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:scorbit_sdk>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()

# ---- Add Tests ----

enable_testing()

# Note: Catch2, doctest and similar testing frameworks can automatically configure CMake tests. For other
# testing frameworks add the tests target instead: add_test(NAME ${PROJECT_NAME} COMMAND
# ${PROJECT_NAME})

# include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
# doctest_discover_tests(${PROJECT_NAME})

include(CTest)
include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(${PROJECT_NAME})

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
    target_compile_options(scorbit_sdk PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
    target_link_options(scorbit_sdk PUBLIC -fprofile-arcs -ftest-coverage)
endif()
