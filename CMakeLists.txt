cmake_minimum_required(VERSION 3.28)

# Read version from VERSION file
if(NOT DEFINED SCORBIT_SDK_VERSION)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    )
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SCORBIT_SDK_VERSION)
    string(STRIP "${SCORBIT_SDK_VERSION}" SCORBIT_SDK_VERSION)
endif()

# ---- Project ----

project(scorbit_sdk VERSION ${SCORBIT_SDK_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- CMake directory for libs ----
set(MY_CMAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(${MY_CMAKE_DIR}/tools.cmake)
include(${MY_CMAKE_DIR}/ArchDetect.cmake)

# ---- Options ----

option(SCORBIT_SDK_PRODUCTION "Production build for distribution" OFF)
option(SCORBIT_SDK_SHARED "Build shared library" ON)

set(SCORBIT_LOGGER "callback" CACHE STRING "Logger backend to use (callback or spdlog)")
set_property(CACHE SCORBIT_LOGGER PROPERTY STRINGS callback spdlog)

set(SCORBIT_SDK_ABI "unknown" CACHE STRING "ABI tag for self update")

set(SCORBIT_SDK_INSTALL_PREFIX "/usr/local")
set(SCORBIT_SDK_PLATFORM_ID "${TARGET_ARCH}_${SCORBIT_SDK_ABI}")

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(
        FATAL_ERROR
        "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
endif()

if(SCORBIT_SDK_PRODUCTION AND SCORBIT_SDK_ABI STREQUAL "unknown")
    message(
        FATAL_ERROR
        "Production build requires SCORBIT_SDK_ABI to be set. Use -DSCORBIT_SDK_ABI=<abi>"
    )
endif()

# ---- Project settings ----

if(SCORBIT_SDK_SHARED)
    set(BUILD_TYPE SHARED)
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

# To silence warning in PackageProject
if(POLICY CMP0177)
    set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)
endif()
# PackageProject.cmake will be used to make our target installable
CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.13.0")

include(${MY_CMAKE_DIR}/lib_fmt.cmake)

find_package(OpenSSL REQUIRED)

include(${MY_CMAKE_DIR}/lib_cpr.cmake)

if(APPLE AND NOT DEFINED LibArchive_ROOT)
    set(LibArchive_ROOT "/opt/homebrew/opt/libarchive")
endif()
find_package(LibArchive REQUIRED) # Try to find system-installed libarchive

# Boost -----------

set(BOOST_COMPILED_COMPONENTS filesystem thread url)
set(BOOST_HEADER_ONLY_COMPONENTS asio uuid dll signals2)
include(${MY_CMAKE_DIR}/lib_boost.cmake)

include(${MY_CMAKE_DIR}/lib_nlohmann_json.cmake)
include(${MY_CMAKE_DIR}/lib_centrifugo-cpp.cmake)

CPMAddPackage(
    NAME logger
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/libs/logger
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS "SCORBIT_LOGGER=${SCORBIT_LOGGER}"
)

CPMAddPackage(
    NAME tpm
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/libs/tpm
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
)

CPMAddPackage(
    NAME nfc
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/libs/nfc
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
)

# --- Add resources ---

include(${MY_CMAKE_DIR}/CMakeRC.cmake)

# download certs
include(${MY_CMAKE_DIR}/download_file.cmake)
download_file(
    "https://curl.se/ca/cacert.pem"
    "${CMAKE_CURRENT_BINARY_DIR}/"
)
set(CA_CERT_FILE "${DOWNLOADED_FILE}")

cmrc_add_resource_library(
    my-resources
    ALIAS scorbit::rc
    NAMESPACE scorbit
    WHENCE "${CMAKE_CURRENT_BINARY_DIR}/"
    ${CA_CERT_FILE}
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
        VERSION
        source/net.cpp
        source/config_c.cpp
        source/game_state_c.cpp
        source/game_state_impl.h
        source/game_state_impl.cpp
        include/scorbit_sdk/config_c.h
        include/scorbit_sdk/config.h
        include/scorbit_sdk/game_state_c.h
        include/scorbit_sdk/game_state.h
        include/scorbit_sdk/scorbit_sdk.h
        include/scorbit_sdk/scorbit_sdk_c.h
        include/scorbit_sdk/log.h
        include/scorbit_sdk/log_types.h
        include/scorbit_sdk/common_types_c.h
        include/scorbit_sdk/log_types_c.h
        include/scorbit_sdk/log_c.h
        include/scorbit_sdk/event_types_c.h
        include/scorbit_sdk/event_helpers_c.h
        include/scorbit_sdk/event_types.h
        include/scorbit_sdk/event.h
        source/net_base.h
        source/net.h
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
        source/worker.h
        source/worker.cpp
        source/updater.h
        source/updater.cpp
        source/utils/mac_address.h
        source/utils/mac_address.cpp
        source/utils/decrypt.h
        source/utils/decrypt.cpp
        source/utils/signer.h
        source/utils/date_time_parser.h
        source/utils/date_time_parser.cpp
        source/utils/jwt_parser.h
        source/utils/jwt_parser.cpp
        source/utils/archiver.h
        source/utils/archiver.cpp
        include/scorbit_sdk/player_info.h
        source/player_profiles_manager.h
        source/player_profiles_manager.cpp
        source/utils/lru_cache.hpp
        source/safe_multipart.h
        source/fmt_formatters.h
        source/identifiers.h
        source/event_helpers_c.cpp
        source/event_classes.h
        source/event_queue.h
        source/event_manager.h
        source/dflags.h
        source/session_flags.h
        source/key_resolver.h
        source/nfc_tpm_key_resolver.h
        source/nfc_tpm_key_resolver.cpp
        source/provisioning_client.h
        source/provisioning_client.cpp
        source/soft_key_resolver.h
        source/soft_key_resolver.cpp
)

# Conditionally include callback logger API implementation
if(SCORBIT_LOGGER STREQUAL "callback")
    target_sources(${PROJECT_NAME} PRIVATE source/log_c.cpp)
    target_compile_definitions(${PROJECT_NAME} PUBLIC SCORBIT_LOGGER_CALLBACK)
elseif(SCORBIT_LOGGER STREQUAL "spdlog")
    target_compile_definitions(${PROJECT_NAME} PUBLIC SCORBIT_LOGGER_SPDLOG)
endif()

# Generate the header from the template
configure_file(source/platform_id.h.in "${CMAKE_CURRENT_BINARY_DIR}/platform_id.h" @ONLY)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")
# target_compile_options(${PROJECT_NAME} PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)

# To exclude symbols from other static libraries
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--exclude-libs,ALL")
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-dead_strip")
endif()

# BeableBone Black (BBB) requires using static libstdc++ which will link to libstdc++_pic
# it should be installed via libstdc++-9-pic package
if(SCORBIT_SDK_ABI STREQUAL "u12" OR SCORBIT_SDK_ABI STREQUAL "u12dev")
    target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++)
endif()

# Link dependencies
target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        $<BUILD_INTERFACE:scorbit::rc>
        ${BOOST_LIBS}
        fmt::fmt
        cpr::cpr
        OpenSSL::Crypto
        LibArchive::LibArchive
        centrifugo-cpp
        nfc
        $<BUILD_INTERFACE:tpm>
        $<BUILD_INTERFACE:logger>
)
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PUBLIC dl)
endif()

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE 3rd/obfuscate "${CMAKE_CURRENT_BINARY_DIR}"
    # $<INSTALL_INTERFACE:include>
)

include(${MY_CMAKE_DIR}/encrypt.cmake)
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        SCORBIT_SDK_ENCRYPT_SECRET="${SCORBIT_SDK_ENCRYPT_SECRET}"
        SCORBIT_SDK_THIS_KEY_HASH="${SCORBIT_SDK_THIS_KEY_HASH}"
        SCORBIT_SDK_PRODUCTION_KEY_HASH="${SCORBIT_SDK_PRODUCTION_KEY_HASH}"
)

# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.

# the location where the project's version header will be placed should match the project's regular
# header paths
string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)
string(TOLOWER ${PROJECT_NAME}/export.h EXPORT_HEADER_LOCATION)

# Detect architecture

# Set package file name to include architecture
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${SCORBIT_SDK_PLATFORM_ID}")

# Force correct install paths before packageProject() - cryptoauth sets CMAKE_INSTALL_PREFIX to "/"
# which causes GNUInstallDirs to use /usr/lib instead of lib
set(CMAKE_INSTALL_PREFIX "${SCORBIT_SDK_INSTALL_PREFIX}" CACHE PATH "" FORCE)
set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "" FORCE)

set_property(TARGET scorbit_sdk PROPERTY EXPORT_NAME scorbit_sdk)

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
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
# set(CPACK_DEBIAN_PACKAGE_DEPENDS "unzip, coreutils, wget, sed, grep, apt, bash")

set(CPACK_DEBIAN_PACKAGE_SECTION "General")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CPACK_STRIP_FILES TRUE)
else()
    set(CPACK_STRIP_FILES FALSE)
endif()

set(CPACK_PACKAGING_INSTALL_PREFIX ${SCORBIT_SDK_INSTALL_PREFIX})

include(CPack)
