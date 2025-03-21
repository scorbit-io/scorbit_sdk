cmake_minimum_required(VERSION 3.28)

# ---- Project ----

# Note: update this to your new project's name and version
project(scorbit_sdk VERSION 1.0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Options ----

option(SCORBIT_SDK_PRODUCTION "Production build for distribution" OFF)
option(SCORBIT_SDK_SHARED "Build shared library" ON)
option(BBB_BUILD "Build library for BeagleBone Black with packaging extra libs" OFF)

set(SCORBIT_SDK_INSTALL_PREFIX "/usr/local")

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
CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.13.0")

CPMAddPackage(
    NAME fmt
    GIT_TAG 10.2.1
    GITHUB_REPOSITORY fmtlib/fmt
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS "FMT_INSTALL YES"
    GIT_SHALLOW TRUE
)

find_package(OpenSSL REQUIRED)

find_package(CURL 8.0.0)
if(CURL_FOUND)
    set(CPR_USE_SYSTEM_CURL ON)
endif()
set(BUILD_SHARED_LIBS OFF)
CPMAddPackage(
    NAME cpr
    GIT_TAG 1.11.0
    GITHUB_REPOSITORY libcpr/cpr
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    GIT_SHALLOW TRUE
)

# Boost -----------

set(TRY_BOOST_VERSION 1.87.0)
set(BOOST_NOT_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED thread)
set(BOOST_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED asio uuid json flyweight)

include(cmake/boost.cmake)

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
        source/utils/bytearray.cpp
        source/utils/bytearray.h
        source/worker.h
        source/worker.cpp
        source/utils/mac_address.h
        source/utils/mac_address.cpp
        source/utils/decrypt.h
        source/utils/decrypt.cpp
        source/utils/signer.h
        source/utils/date_time_parser.h
        source/utils/date_time_parser.cpp
)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")
# target_compile_options(${PROJECT_NAME} PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)

# To exclude symbols from other static libraries
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--exclude-libs,ALL")
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-dead_strip")
elseif(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE "/NODEFAULTLIB:LIBCMT")
endif()

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE ${BOOST_LIBS} fmt::fmt cpr::cpr OpenSSL::Crypto)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE 3rd/obfuscate
    # $<INSTALL_INTERFACE:include>
)

include(cmake/encrypt.cmake)
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE SCORBIT_SDK_ENCRYPT_SECRET="${SCORBIT_SDK_ENCRYPT_SECRET}"
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
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/"
        COMPONENT "${PROJECT_NAME}_Runtime"
    )
endif()

# Detect architecture
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ARCHITECTURE)

# Map architecture identifiers to standardized suffixes used in packaging
if(ARCHITECTURE STREQUAL "armv7l")
    set(ARCHITECTURE "armhf")
elseif(ARCHITECTURE STREQUAL "aarch64")
    set(ARCHITECTURE "arm64")
elseif(ARCHITECTURE STREQUAL "x86_64")
    set(ARCHITECTURE "amd64")
endif()

# Set package file name to include architecture
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${ARCHITECTURE}")

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

set(CPACK_PACKAGING_INSTALL_PREFIX ${SCORBIT_SDK_INSTALL_PREFIX})

include(CPack)
