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

# The SDK is a shared library linking static dependencies (fmt, cpr, ...), so they must be PIC.
# tools.cmake sets this only for a top-level project and the subdirectories include it from their
# own, so without this a Linux build of the suite fails to link (SB-6104).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

# The VBScript binding: its parity with GameState wherever Python 3 runs, its cscript test on Windows.
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/vbscript
    ${CMAKE_CURRENT_BINARY_DIR}/vbscript
)

# The Python wrappers' ctypes declarations against the C prototypes, and every C function's use by
# the C++ and Python APIs (SB-6151). Both read from source and need no SDK build, so they run
# wherever a Python 3 interpreter is found.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME python_bindings_abi
        COMMAND
            ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_python/test_bindings_abi.py
    )
    add_test(
        NAME api_parity
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_python/test_api_parity.py
    )

    # The wrapper itself, imported from source against the SDK built here. Not -I: it needs
    # PYTHONPATH. Only against a shared build: ctypes cannot load a static archive, and the loader
    # would fall back to whatever SDK is installed. test_scrobit.py is left out: it targets the
    # pre-Config wrapper and cannot import.
    if(TARGET scorbit_sdk)
        get_target_property(_scorbit_sdk_type scorbit_sdk TYPE)
    endif()
    if(_scorbit_sdk_type STREQUAL "SHARED_LIBRARY" AND NOT WIN32)
        foreach(_module test_leaderboard_types test_wrapper_parity)
            add_test(
                NAME python_${_module}
                COMMAND ${Python3_EXECUTABLE} -m unittest -v ${_module}
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test_python
            )
            set_tests_properties(
                python_${_module}
                PROPERTIES
                    ENVIRONMENT
                        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/python/src;SCORBIT_SDK_PATH=$<TARGET_FILE_DIR:scorbit_sdk>"
            )
        endforeach()
    endif()
endif()
