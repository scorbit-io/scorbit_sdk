# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

cmake_minimum_required(VERSION 3.28)

project(test)

add_subdirectory(test_detail)
add_subdirectory(test_interface)
if(NOT MSVC)
    add_subdirectory(test_interface_c)
endif()
