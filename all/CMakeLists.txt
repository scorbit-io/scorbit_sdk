# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# this script adds all subprojects to a single build to allow IDEs understand the full project
# structure.

cmake_minimum_required(VERSION 3.28)

project(scorbit_sdk_all LANGUAGES CXX)

include(../cmake/tools.cmake)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../tests ${CMAKE_BINARY_DIR}/tests)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../documentation ${CMAKE_BINARY_DIR}/documentation)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../examples ${CMAKE_BINARY_DIR}/examples)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../encrypt_tool/ ${CMAKE_BINARY_DIR}/encrypt_tool)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../wrappers/python ${CMAKE_BINARY_DIR}/python)
