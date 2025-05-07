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

if(APPLE AND NOT DEFINED LibArchive_ROOT)
    set(LibArchive_ROOT "/opt/homebrew/opt/libarchive")
endif()
find_package(LibArchive REQUIRED) # Try to find system-installed libarchive

# Boost -----------

set(BOOST_COMPILED_COMPONENTS filesystem json thread )
set(BOOST_HEADER_ONLY_COMPONENTS asio uuid flyweight dll)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/boost.cmake)

# scorbit_sdk
if(TEST_INSTALLED_VERSION)
    find_package(scorbit_sdk REQUIRED)
else()
    CPMAddPackage(
        NAME scorbit_sdk
        SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..
    )
endif()

find_package(OpenSSL REQUIRED)

# ---- Create binary ----

add_executable(${PROJECT_NAME})

target_sources(
    test_detail
    PRIVATE
        ../../include/scorbit_sdk/log.h
        ../../source/log.cpp
        ../../source/logger.h
        ../../source/logger.cpp
        ../../source/player_state.h
        ../../source/player_state.cpp
        ../../include/scorbit_sdk/game_state.h
        ../../source/game_state_impl.h
        ../../source/game_state_impl.cpp
        source/test_player_state.cpp
        ../../source/modes.h
        ../../source/modes.cpp
        source/test_modes.cpp
        source/test_game_state.cpp
        ../../source/game_data.h
        source/trompeloeil_printer.h
        ../../source/updater.h
        ../../source/updater.cpp
        ../../source/utils/archiver.h
        ../../source/utils/archiver.cpp
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
        source/platform_id.h
        source/test_updater.cpp
        source/test_logger.cpp
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        ${BOOST_LIBS}
        Catch2::Catch2WithMain
        trompeloeil::trompeloeil
        fmt::fmt
        OpenSSL::Crypto
        cpr::cpr
        LibArchive::LibArchive
)
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE dl)
endif()

target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
        "${scorbit_sdk_SOURCE_DIR}/include"
        "${scorbit_sdk_SOURCE_DIR}/source"
        "${scorbit_sdk_BINARY_DIR}/PackageProjectInclude"
)

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
        # We are building manually source files which will be statically built-in to test main
        SCORBIT_SDK_STATIC_DEFINE=1
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
    target_compile_options(${PROJECT_NAME} PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
    target_link_options(${PROJECT_NAME} PUBLIC -fprofile-arcs -ftest-coverage)
endif()
