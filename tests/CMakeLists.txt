# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

cmake_minimum_required(VERSION 3.28)

project(test)

# The test subdirectories include the repo's cmake modules through MY_CMAKE_DIR,
# but only the ROOT CMakeLists.txt sets it and this project is never added by
# the root -- it is configured on its own (cmake -S tests -B build). Standalone,
# the variable was empty and every one of those includes resolved to "/CPM.cmake",
# so the tests project could not configure at all.
set(MY_CMAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)

# Register this directory with CTest so a top-level CTestTestfile.cmake is generated and the
# subdirectories' tests are reachable from the build root. Without it, `ctest` run from the build
# root reports "No test configuration file found!" and exits 0 — a pass having run nothing.
enable_testing()

# Tests must never take the real tap pad lock (libs/tpm/README-tappad-lock.md). Test binaries that
# reach cryptoauthlib only through the SDK library get this path from the environment.
set(SCORBIT_TAPPAD_LOCK_TEST_ENV ON)
set(SCORBIT_TAPPAD_LOCK_TEST_PATH ${CMAKE_BINARY_DIR}/scorbit-tappad-test.lock)

add_subdirectory(test_detail)
add_subdirectory(test_interface)
if(NOT MSVC)
    add_subdirectory(test_interface_c)
endif()

# The Python wrappers' ctypes declarations against the C prototypes. It reads both from source
# and needs no SDK build, so it runs wherever a Python 3 interpreter is found.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME python_bindings_abi
        COMMAND
            ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_python/test_bindings_abi.py
    )
endif()
