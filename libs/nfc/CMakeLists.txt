cmake_minimum_required(VERSION 3.28...4.0)

project(nfc VERSION 1.0.0 LANGUAGES CXX)

include(${MY_CMAKE_DIR}/guards.cmake)
include(${MY_CMAKE_DIR}/tools.cmake)

include(${MY_CMAKE_DIR}/package_project.cmake)

include(${MY_CMAKE_DIR}/lib_fmt.cmake)
include(${MY_CMAKE_DIR}/lib_date.cmake)

# ---- Create library ----

include(sources.cmake)
add_library(${PROJECT_NAME} STATIC ${headers} ${sources})

target_compile_definitions(${PROJECT_NAME} PUBLIC USE_FMT_LIBRARY USE_DATE_LIBRARY)
target_link_libraries(${PROJECT_NAME} PUBLIC fmt::fmt date::date)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)

# ---- Create an installable target ----

string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)

packageProject(
    NAME ${PROJECT_NAME}
    VERSION ${PROJECT_VERSION}
    NAMESPACE ${PROJECT_NAME}
    BINARY_DIR ${PROJECT_BINARY_DIR}
    INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include
    INCLUDE_DESTINATION include/${PROJECT_NAME}-${PROJECT_VERSION}
    VERSION_HEADER "${VERSION_HEADER_LOCATION}"
    COMPATIBILITY SameMajorVersion
)
