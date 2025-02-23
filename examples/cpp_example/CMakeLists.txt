cmake_minimum_required(VERSION 3.16)

project(cpp_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(scorbit_sdk)

if(scorbit_sdk_FOUND)
    message(STATUS "scorbit_sdk found")
else()
    message(STATUS "scorbit_sdk not found, using CPM to add it")
    CPMAddPackage(NAME scorbit_sdk SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
endif()

find_package(OpenSSL REQUIRED)

add_executable(
    ${PROJECT_NAME}
    main.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE scorbit_sdk::scorbit_sdk OpenSSL::Crypto)
