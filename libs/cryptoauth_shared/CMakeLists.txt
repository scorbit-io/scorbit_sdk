# Standalone shared build of Microchip cryptoauthlib (same options/patches as the SDK via lib_cryptoauth.cmake).
cmake_minimum_required(VERSION 3.28)

project(scorbit_cryptoauth_shared LANGUAGES C)

set(MY_CMAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake)
include(${MY_CMAKE_DIR}/tools.cmake)

# Link libcrypto.a into libcryptoauth.so (same sysroot OpenSSL as production SDK builds).
set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
find_package(OpenSSL REQUIRED)

set(SCORBIT_CRYPTOAUTH_BUILD_SHARED ON)
include(${CMAKE_CURRENT_SOURCE_DIR}/../tpm/lib_cryptoauth.cmake)

if(NOT TARGET cryptoauth)
    message(FATAL_ERROR "cryptoauth target was not added by lib_cryptoauth.cmake")
endif()

set_target_properties(
    cryptoauth
    PROPERTIES
        OUTPUT_NAME cryptoauth
        VERSION 3.7.9
        SOVERSION 3
)

# Match SDK production packages (CPACK_STRIP_FILES). Link-time strip: cryptoauth is
# defined in CPM's subdirectory, so add_custom_command(TARGET ...) is not allowed here.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(APPLE)
        target_link_options(cryptoauth PRIVATE "-Wl,-dead_strip")
    else()
        target_link_options(cryptoauth PRIVATE "-Wl,-s")
    endif()
endif()

# cryptoauthlib already links ${OPENSSL_CRYPTO_LIBRARY} (plain signature); match that style here.
if(cryptoauth_ADDED AND UNIX AND NOT APPLE)
    target_link_libraries(cryptoauth dl pthread)
endif()

if(cryptoauth_ADDED)
    install(
        TARGETS cryptoauth
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
