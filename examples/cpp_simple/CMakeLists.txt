cmake_minimum_required(VERSION 3.16)

project(cpp_simple LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

CPMAddPackage(NAME scorbit_sdk SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)

find_package(OpenSSL REQUIRED)

add_executable(
    ${PROJECT_NAME}
    main.cpp
    ../common_signer/scorbit_crypt.c
    ../common_signer/scorbit_crypt.h
)
target_link_libraries(${PROJECT_NAME} PRIVATE scorbit_sdk::scorbit_sdk OpenSSL::Crypto)
