# this script adds all subprojects to a single build to allow IDEs understand the full project
# structure.

cmake_minimum_required(VERSION 3.20)

project(examples)

add_subdirectory(cpp_example)
add_subdirectory(c_example)
