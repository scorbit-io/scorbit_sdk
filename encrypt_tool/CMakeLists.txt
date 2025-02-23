cmake_minimum_required(VERSION 3.28)

# ---- Project ----

# Note: update this to your new project's name and version
project(encrypt_tool LANGUAGES CXX)

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(
        FATAL_ERROR
        "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
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

# ---- Add dependencies ----

find_package(OpenSSL REQUIRED)

# ---- Create executable ----

add_executable(${PROJECT_NAME})

set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES CXX_STANDARD 17 CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN 1
)

target_sources(${PROJECT_NAME} PRIVATE source/main.cpp)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE ../3rd/obfuscate
)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

include(../cmake/encrypt.cmake)
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE SCORBIT_SDK_ENCRYPT_SECRET="${SCORBIT_SDK_ENCRYPT_SECRET}"
)

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::Crypto)

# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.

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

set(CPACK_GENERATOR DEB)

set(CPACK_PACKAGE_VENDOR "scorbit.io")
set(CPACK_PACKAGE_CONTACT "Dilshod Mukhtarov <dilshodm@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "encrypt_tool")

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")

# Generate Deb dependencies
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_DEBIAN_PACKAGE_SECTION "General")

set(CPACK_STRIP_FILES TRUE)

set(CPACK_PACKAGING_INSTALL_PREFIX ${SCORBIT_SDK_INSTALL_PREFIX})

include(CPack)
