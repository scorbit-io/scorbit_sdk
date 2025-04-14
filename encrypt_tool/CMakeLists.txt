cmake_minimum_required(VERSION 3.28)

# ---- Project ----

# Note: update this to your new project's name and version
project(encrypt_tool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(../cmake/tools.cmake)

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
set(CMAKE_INSTALL_RPATH $ORIGIN $ORIGIN/${relDir} "/usr/local/lib")

# ---- Add dependencies ----
find_package(OpenSSL REQUIRED)

find_package(ZLIB)

# ---- Create executable ----

add_executable(${PROJECT_NAME})

set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES CXX_STANDARD 17 CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN 1
)

target_sources(${PROJECT_NAME} PRIVATE source/main.cpp)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/obfuscate
)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

# To exclude symbols from other static libraries
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--exclude-libs,ALL")
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-dead_strip")
elseif(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE "/NODEFAULTLIB:LIBCMT")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/encrypt.cmake)
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE SCORBIT_SDK_ENCRYPT_SECRET="${SCORBIT_SDK_ENCRYPT_SECRET}"
)

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::Crypto ZLIB::ZLIB)

install(
    TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

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

set(CPACK_GENERATOR TGZ)

set(CPACK_PACKAGE_VENDOR "scorbit.io")
set(CPACK_PACKAGE_CONTACT "Dilshod Mukhtarov <dilshodm@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "encrypt_tool")

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")

set(CPACK_STRIP_FILES TRUE)

set(CPACK_PACKAGING_INSTALL_PREFIX ${SCORBIT_SDK_INSTALL_PREFIX})

include(CPack)
