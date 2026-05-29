cmake_minimum_required(VERSION 3.28...4.0)

project(diagnostics VERSION 1.0.0 LANGUAGES CXX)

if (NOT DEFINED MY_CMAKE_DIR)
    set(MY_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../cmake)
endif()

include(${MY_CMAKE_DIR}/guards.cmake)
include(${MY_CMAKE_DIR}/tools.cmake)
include(${MY_CMAKE_DIR}/package_project.cmake)

set(BOOST_COMPILED_COMPONENTS filesystem thread url)
set(BOOST_HEADER_ONLY_COMPONENTS asio uuid dll signals2 flyweight)
include(${MY_CMAKE_DIR}/lib_boost.cmake)
include(${MY_CMAKE_DIR}/lib_nlohmann_json.cmake)

include(sources.cmake)

add_library(${PROJECT_NAME} STATIC ${headers} ${sources})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

target_link_libraries(${PROJECT_NAME} PUBLIC ${BOOST_LIBS} nlohmann_json::nlohmann_json)

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PUBLIC dl)
endif()

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source
)

add_executable(diagnostics_wifi_status examples/wifi_status/main.cpp)
target_link_libraries(diagnostics_wifi_status PRIVATE diagnostics)
set_target_properties(diagnostics_wifi_status PROPERTIES CXX_STANDARD 20)

if(SCORBIT_AUX_BUILD_TESTS OR (PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR))
    enable_testing()
endif()
