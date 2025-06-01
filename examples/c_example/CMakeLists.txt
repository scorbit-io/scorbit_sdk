# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

    cmake_minimum_required(VERSION 3.16)

project(c_example LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(scorbit_sdk)

if(scorbit_sdk_FOUND)
    message(STATUS "scorbit_sdk found")
else()
    message(STATUS "scorbit_sdk not found, using CPM to add it")
    CPMAddPackage(NAME scorbit_sdk SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
endif()

add_executable(
    ${PROJECT_NAME}
    main.c
)
target_link_libraries(${PROJECT_NAME} PRIVATE scorbit_sdk::scorbit_sdk)
