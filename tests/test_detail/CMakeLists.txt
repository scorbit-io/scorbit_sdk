cmake_minimum_required(VERSION 3.28)

project(test_detail LANGUAGES CXX)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

if(TEST_INSTALLED_VERSION)
    message(STATUS "Testing installed version, detail can't be tested")
    return()
endif()

# --- Import tools ----

include(../../cmake/tools.cmake)

# ---- Dependencies ----

include(../../cmake/CPM.cmake)

CPMAddPackage("gh:TheLartians/Format.cmake@1.7.3")

# Catch2
CPMAddPackage(
    NAME Catch2
    GIT_TAG v3.7.1
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_SHALLOW TRUE
    OPTIONS "CATCH_INSTALL_DOCS OFF" "CATCH_INSTALL_HELPERS OFF" "CATCH_INSTALL_TESTS OFF"
)

# Trompeloeil
CPMAddPackage(
    NAME trompeloeil
    GIT_TAG v48
    GIT_SHALLOW TRUE
    GITHUB_REPOSITORY rollbear/trompeloeil
    OPTIONS "TROMPELOEIL_INSTALL_DOCS OFF"
)

# Boost -----------

set(TRY_BOOST_VERSION 1.87.0)
set(BOOST_NOT_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED thread)
set(BOOST_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED asio uuid json flyweight)

if(WIN32)
    set(BOOST_COMPONENTS
        ${BOOST_NOT_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED}
        ${BOOST_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED}
    )
    find_package(Boost REQUIRED COMPONENTS ${BOOST_COMPONENTS})
    set(BOOST_LIBS ${Boost_LIBRARIES})
else()
    include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/boost.cmake)
endif()

# scorbit_sdk
if(TEST_INSTALLED_VERSION)
    find_package(scorbit_sdk REQUIRED)
else()
    CPMAddPackage(NAME scorbit_sdk SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
endif()

find_package(OpenSSL REQUIRED)

# ---- Special build encrypt_tool with SCORBIT_SDK_ENCRYPT_SECRET env var ----

# Define the test key, provider and secret
set(TEST_KEY "07D54D8B3B2743550071639FDA6F5ED7BB0407C479FDA3A5CDBD22B09870DCF9")
set(TEST_PROVIDER "test")
set(TEST_SECRET "secret")

# Set the environment variable for building encrypt_tool
set(ENCRYPT_TOOL_TEST_BUILD_PATH "${CMAKE_CURRENT_BINARY_DIR}/encrypt_tool_test")

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    set(TOOLCHAIN_DEFINITION "-D CMAKE_TOOLCHAIN_FILE='${CMAKE_TOOLCHAIN_FILE}'")
endif()

if(DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_DEFINITION "-D VCPKG_TARGET_TRIPLET='${VCPKG_TARGET_TRIPLET}'")
endif()

if(DEFINED CMAKE_GENERATOR)
    set(GENERATOR "-G ${CMAKE_GENERATOR}")
endif()

# Configure encrypt_tool with env var SCORBIT_SDK_ENCRYPT_SECRET
set(ENV{SCORBIT_SDK_ENCRYPT_SECRET} ${TEST_SECRET})
execute_process(
    COMMAND ${CMAKE_COMMAND} -B ${ENCRYPT_TOOL_TEST_BUILD_PATH}
            -S ${CMAKE_CURRENT_SOURCE_DIR}/../../encrypt_tool
            -D SCORBIT_SDK_PRODUCTION=OFF
            ${TOOLCHAIN_DEFINITION}
            ${VCPKG_DEFINITION}
            ${GENERATOR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND_ERROR_IS_FATAL ANY
)
unset(ENV{SCORBIT_SDK_ENCRYPT_SECRET})

# Build encrypt_tool
execute_process(
    COMMAND ${CMAKE_COMMAND} --build ${ENCRYPT_TOOL_TEST_BUILD_PATH} --parallel
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND_ERROR_IS_FATAL ANY
)

# Define the correct executable path
set(TEST_ENCRYPT_TOOL_EXECUTABLE "${ENCRYPT_TOOL_TEST_BUILD_PATH}/encrypt_tool")

# Run encrypt_tool to obtain the encrypted key
execute_process(
    COMMAND ${TEST_ENCRYPT_TOOL_EXECUTABLE} ${TEST_KEY} ${TEST_PROVIDER}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_VARIABLE TEST_ENCRYPTED_KEY
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Encrypted secret for tests: ${TEST_ENCRYPTED_KEY}")

# ---- Create binary ----

add_executable(${PROJECT_NAME})

target_sources(
    test_detail
    PRIVATE
        ../../source/player_state.h
        ../../source/player_state.cpp
        source/test_player_state.cpp
        ../../source/modes.h
        ../../source/modes.cpp
        source/test_modes.cpp
        source/test_game_state.cpp
        ../../source/game_data.h
        source/trompeloeil_printer.h
        ../../source/net.h
        ../../source/net.cpp
        source/test_net.cpp
        ../../source/utils/bytearray.h
        ../../source/utils/bytearray.cpp
        ../../source/net_util.h
        ../../source/net_util.cpp
        source/test_net_util.cpp
        ../../source/worker.cpp
        ../../source/worker.h
        source/test_worker.cpp
        ../../source/utils/mac_address.h
        ../../source/utils/mac_address.cpp
        source/test_mac_address.cpp
        ../../source/utils/decrypt.h
        ../../source/utils/decrypt.cpp
        source/test_decrypt.cpp
        ../../source/utils/date_time_parser.h
        ../../source/utils/date_time_parser.cpp
        source/test_date_time_parser.cpp
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        ${BOOST_LIBS}
        scorbit_sdk::scorbit_sdk
        Catch2::Catch2WithMain
        trompeloeil::trompeloeil
        fmt::fmt
        OpenSSL::Crypto
        cpr::cpr
)

target_include_directories(${PROJECT_NAME} PRIVATE ${scorbit_sdk_SOURCE_DIR}/source)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

# enable compiler warnings
if(NOT TEST_INSTALLED_VERSION)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(scorbit_sdk PUBLIC -Wall -Wpedantic -Wextra -Werror)
    elseif(MSVC)
        # target_compile_options(scorbit_sdk PUBLIC /W4 /WX)
    endif()
endif()

# Pass compile definitions
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        TEST_ENCRYPTED_KEY="${TEST_ENCRYPTED_KEY}"
        TEST_KEY="${TEST_KEY}"
        TEST_PROVIDER="${TEST_PROVIDER}"
        TEST_SECRET="${TEST_SECRET}"
)

if (WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:scorbit_sdk>
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
