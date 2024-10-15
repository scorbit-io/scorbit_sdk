cmake_minimum_required(VERSION 3.28)

# ---- Project ----

# Note: update this to your new project's name and version
project(scorbit_sdk VERSION 1.0.0 LANGUAGES CXX)

# ---- Options ----

option(SCORBIT_SDK_SHARED "Build shared library" ON)
option(BBB_BUILD "Build library for BeagleBone Black with packaging extra libs" OFF)

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

include(GNUInstallDirs)
# Set the RPATH config to be relative to the install location, so extra libs will be first searched
# in the same directory
file(
    RELATIVE_PATH
    relDir
    ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
    ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
)
set(CMAKE_INSTALL_RPATH $ORIGIN $ORIGIN/${relDir})

# ---- Add dependencies via CPM ----
# see https://github.com/TheLartians/CPM.cmake for more info

include(cmake/CPM.cmake)

# PackageProject.cmake will be used to make our target installable
# CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.12.0")
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

add_library(scorbit_sdk ${BUILD_TYPE})

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
    scorbit_sdk
    PRIVATE
        source/net.cpp
        source/logger.cpp
        source/logger.h
        source/log.cpp
        source/game_state_c.cpp
        source/game_state.cpp
        source/game_state_impl.h
        source/game_state_impl.cpp
        include/scorbit_sdk/game_state_c.h
        include/scorbit_sdk/game_state.h
        include/scorbit_sdk/scorbit_sdk.h
        include/scorbit_sdk/scorbit_sdk_c.h
        include/scorbit_sdk/net_base.h
        source/net.h
        include/scorbit_sdk/log.h
        include/scorbit_sdk/log_types.h
        include/scorbit_sdk/common_types_c.h
        include/scorbit_sdk/log_types_c.h
        include/scorbit_sdk/log_c.h
        source/log_c.cpp
        source/player_state.h
        source/player_state.cpp
        source/modes.h
        source/modes.cpp
        source/game_data.h
        include/scorbit_sdk/net_types_c.h
        include/scorbit_sdk/net_types.h
        source/net_util.h
        source/net_util.cpp
        include/scorbit_sdk/game_state_factory.h
        source/game_state_factory.cpp
)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    # $<INSTALL_INTERFACE:include>
)

# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.

# the location where the project's version header will be placed should match the project's regular
# header paths
string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)
string(TOLOWER ${PROJECT_NAME}/export.h EXPORT_HEADER_LOCATION)

# BeableBone Black (BBB) requires installing extra libs
if(BBB_BUILD)
    install(
        FILES
            "/usr/lib/arm-linux-gnueabihf/libstdc++.so.6"
            "/usr/lib/arm-linux-gnueabihf/libstdc++.so.6.0.28"
            "/usr/local/lib/libssl.so"
            "/usr/local/lib/libssl.so.3"
            "/usr/local/lib/libcrypto.so"
            "/usr/local/lib/libcrypto.so.3"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/"
        COMPONENT "${PROJECT_NAME}_Development"
    )

    install(
        DIRECTORY "/usr/local/include/openssl"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/"
        COMPONENT "${PROJECT_NAME}_Development"
    )
endif()

packageProject(
    NAME ${PROJECT_NAME}
    VERSION ${PROJECT_VERSION}
    NAMESPACE ${PROJECT_NAME}
    BINARY_DIR ${PROJECT_BINARY_DIR}
    INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include
    INCLUDE_DESTINATION include
    RUNTIME_DESTINATION /
    VERSION_HEADER "${VERSION_HEADER_LOCATION}"
    EXPORT_HEADER "${EXPORT_HEADER_LOCATION}"
    DISABLE_VERSION_SUFFIX YES
)
set(CPACK_GENERATOR DEB)

set(CPACK_PACKAGE_VENDOR "scorbit.io")
set(CPACK_PACKAGE_CONTACT "Dilshod Mukhtarov <dilshodm@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Scorbit SDK")

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")

# Generate Deb dependencies
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# set(CPACK_DEBIAN_PACKAGE_DEPENDS "unzip, coreutils, wget, sed, grep, apt, bash")

set(CPACK_DEBIAN_PACKAGE_SECTION "General")

set(CPACK_STRIP_FILES TRUE)

set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")

include(CPack)
