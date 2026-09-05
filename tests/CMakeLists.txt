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

# Register this directory with CTest so a top-level CTestTestfile.cmake is generated and the
# subdirectories' tests are reachable from the build root. Without it, `ctest` run from the build
# root reports "No test configuration file found!" and exits 0 — a pass having run nothing.
enable_testing()

add_subdirectory(test_detail)
add_subdirectory(test_interface)
if(NOT MSVC)
    add_subdirectory(test_interface_c)
endif()
