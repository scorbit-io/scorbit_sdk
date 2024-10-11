cmake_minimum_required(VERSION 3.28)

# ---- Project ----

project(scorbit_sdk VERSION 0.1.0 LANGUAGES CXX)

# ---- Options ----

option(SCORBIT_SDK_SHARED "Build shared library" ON)

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(
        FATAL_ERROR
        "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
endif()

# ---- Project settings ----

if(SCORBIT_SDK_SHARED)
    set(BUILD_TYPE SHARED)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    message("Building shared library")
else()
    set(BUILD_TYPE STATIC)
    message("Building static library")
endif()

# ---- Add dependencies via CPM ----

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CPM.cmake)

CPMAddPackage("gh:dilshodm/PackageProject.cmake@1.13.0")

CPMAddPackage(
    NAME fmt
    GIT_TAG 10.2.1
    GITHUB_REPOSITORY fmtlib/fmt
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS "FMT_INSTALL YES"
)

# ---- Create library ----

add_library(${PROJECT_NAME} ${BUILD_TYPE})

set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES
        CXX_STANDARD 17
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN 1
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_sources(
    ${PROJECT_NAME}
    PRIVATE
        src/net.cpp
        src/logger.cpp
        src/logger.h
        src/log.cpp
        src/game_state_c.cpp
        src/game_state.cpp
        src/game_state_impl.h
        src/game_state_impl.cpp
        src/log_c.cpp
        src/player_state.h
        src/player_state.cpp
        src/modes.h
        src/modes.cpp
        src/game_data.h
    PUBLIC
        include/scorbit_sdk/game_state_c.h
        include/scorbit_sdk/game_state.h
        include/scorbit_sdk/scorbit_sdk.h
        include/scorbit_sdk/net_base.h
        include/scorbit_sdk/net.h
        include/scorbit_sdk/log.h
        include/scorbit_sdk/log_types.h
        include/scorbit_sdk/common_types_c.h
        include/scorbit_sdk/log_types_c.h
        include/scorbit_sdk/log_c.h
)

target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ---- Create an installable target ----

string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)
string(TOLOWER ${PROJECT_NAME}/export.h EXPORT_HEADER_LOCATION)

packageProject(
    NAME ${PROJECT_NAME}
    VERSION ${PROJECT_VERSION}
    NAMESPACE ${PROJECT_NAME}
    BINARY_DIR ${PROJECT_BINARY_DIR}
    INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include
    INCLUDE_DESTINATION include
    RUNTIME_DESTINATION /
    VERSION_HEADER "${VERSION_HEADER_LOCATION}"
    EXPORT_HEADER "${EXPORT_HEADER_LOCATION}"
    DISABLE_VERSION_SUFFIX YES
)

# ---- Install rules ----

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/ DESTINATION include)

set(CPACK_STRIP_FILES TRUE)
include(CPack)