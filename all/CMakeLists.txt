cmake_minimum_required(VERSION 3.28)

project(scorbit_sdk_all LANGUAGES CXX)

include(../cmake/tools.cmake)

# needed to generate test target
enable_testing()

# Add other subdirectories as needed
# add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../test ${CMAKE_BINARY_DIR}/test)
# add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../documentation ${CMAKE_BINARY_DIR}/documentation)