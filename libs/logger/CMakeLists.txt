cmake_minimum_required(VERSION 3.28...4.0)

# ---- Project ----

project(logger VERSION 1.0.0 LANGUAGES CXX)

# ---- Include guards ----
include(${MY_CMAKE_DIR}/guards.cmake)
include(${MY_CMAKE_DIR}/tools.cmake)

# ---- Options ----

set(SCORBIT_LOGGER "spdlog" CACHE STRING "Logger backend to use (callback or spdlog)")
set_property(CACHE SCORBIT_LOGGER PROPERTY STRINGS callback spdlog)

# ---- Add dependencies via CPM ----

include(${MY_CMAKE_DIR}/lib_fmt.cmake)

# ---- Create library ----

add_library(${PROJECT_NAME} STATIC)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

set(headers
    include/logger/logger.h
)

target_sources(${PROJECT_NAME} PRIVATE ${headers})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

target_link_libraries(${PROJECT_NAME} PUBLIC fmt::fmt)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)

# ---- Select logger backend ----

if(SCORBIT_LOGGER STREQUAL "callback")
    message(STATUS "Logger backend: callback")

    find_package(Threads REQUIRED)

    target_sources(${PROJECT_NAME}
        PRIVATE
            logger_callback/logger/logger_callback.h
            logger_callback/logger_callback.cpp
    )

    target_include_directories(
        ${PROJECT_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/logger_callback>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)

    target_compile_definitions(${PROJECT_NAME} PUBLIC SCORBIT_LOGGER_CALLBACK)

elseif(SCORBIT_LOGGER STREQUAL "spdlog")
    message(STATUS "Logger backend: spdlog")

    include(${MY_CMAKE_DIR}/package_project.cmake)

    CPMAddPackage(
        NAME spdlog
        GIT_TAG v1.17.0-compress
        GITHUB_REPOSITORY dilshodm/spdlog
        SYSTEM YES
        EXCLUDE_FROM_ALL YES
        OPTIONS "SPDLOG_FMT_EXTERNAL ON" "SPDLOG_INSTALL ON" "SPDLOG_FMT_EXTERNAL ON"
    )

    target_sources(${PROJECT_NAME}
        PRIVATE
            logger_spdlog/logger/logger_spdlog.h
            logger_spdlog/logger_spdlog.cpp
    )

    target_include_directories(
        ${PROJECT_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/logger_spdlog>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)

    target_compile_definitions(${PROJECT_NAME} PUBLIC SCORBIT_LOGGER_SPDLOG)

else()
    message(FATAL_ERROR "Invalid SCORBIT_LOGGER value: '${SCORBIT_LOGGER}'. Must be 'callback' or 'spdlog'.")
endif()
