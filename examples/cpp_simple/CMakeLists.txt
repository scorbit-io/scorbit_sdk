cmake_minimum_required(VERSION 3.16)

project(cpp_simple LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

CPMAddPackage(NAME scorbit_sdk SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE scorbit_sdk::scorbit_sdk)
