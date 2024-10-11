# this script adds all subprojects to a single build to allow IDEs understand the full project
# structure.

cmake_minimum_required(VERSION 3.28)

project(test)

add_subdirectory(test_detail)
add_subdirectory(test_interface)
add_subdirectory(test_interface_c)
