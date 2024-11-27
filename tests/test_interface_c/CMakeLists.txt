cmake_minimum_required(VERSION 3.28)

project(scorbit_sdk_tests_c LANGUAGES C)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../../cmake/tools.cmake)

# ---- Dependencies ----

include(../../cmake/CPM.cmake)

CPMAddPackage(
    NAME munit
    GIT_TAG fbbdf1467eb0d04a6ee465def2e529e4c87f2118
    GITHUB_REPOSITORY nemequ/munit
)

if(TEST_INSTALLED_VERSION)
    find_package(scorbit_sdk REQUIRED)
else()
    CPMAddPackage(NAME scorbit_sdk SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
endif()

# ---- Create binary ----

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(${PROJECT_NAME})
target_sources(
    scorbit_sdk_tests_c
    PRIVATE source/test_log_c.c ${munit_SOURCE_DIR}/munit.c ${munit_SOURCE_DIR}/munit.h
)
target_link_libraries(${PROJECT_NAME} scorbit_sdk::scorbit_sdk)
target_include_directories(
    ${PROJECT_NAME}
    PRIVATE ${munit_SOURCE_DIR} ${scorbit_sdk_SOURCE_DIR}/source
)

# enable compiler warnings
if(NOT TEST_INSTALLED_VERSION)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(scorbit_sdk PUBLIC -Wall -Wpedantic -Wextra -Werror)
    elseif(MSVC)
        target_compile_options(scorbit_sdk PUBLIC /W4 /WX)
    endif()
endif()

# ---- Add Tests ----

enable_testing()

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
    target_compile_options(scorbit_sdk PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
    target_link_options(scorbit_sdk PUBLIC -fprofile-arcs -ftest-coverage)
endif()
