# libarchive, preferring the system copy and falling back to a source build.
#
# The SDK needs it for source/utils/archiver.cpp, which builds the diagnostics
# tarball. Linux and macOS have always satisfied it from the system, and that
# keeps working -- this module only adds a fallback for hosts that do not ship
# it, which in practice means Windows, where there was no provision at all and
# the project simply could not be configured.
#
# Same shape as lib_cpr.cmake's handling of curl: look first, fetch only if the
# look fails.

if(APPLE AND NOT DEFINED LibArchive_ROOT)
    set(LibArchive_ROOT "/opt/homebrew/opt/libarchive")
endif()

# NOT REQUIRED -- a miss is the case this module exists to handle.
find_package(LibArchive QUIET)

if(LibArchive_FOUND)
    message(STATUS "libarchive: using the system copy (${LibArchive_LIBRARIES})")
    return()
endif()

message(STATUS "libarchive: not found on this host, building from source")

# zlib comes first and is not optional: it is what the gzip filter needs, and
# gzip is the only filter the SDK writes with.
CPMAddPackage(
    NAME ZLIB
    GITHUB_REPOSITORY madler/zlib
    GIT_TAG v1.3.1
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS "ZLIB_BUILD_EXAMPLES OFF"
)

# Point libarchive's own find_package(ZLIB) at the copy just added, rather than
# letting it search the host. Without this it finds whatever zlib the host
# happens to have -- the system one on macOS, NONE on Windows -- and the two
# failure modes are different and both bad: on macOS it compiles the zlib code
# paths and then fails to link (undefined _inflateInit_, _uncompress, ...),
# and on Windows it would quietly build with NO gzip support at all, which is
# the one filter this SDK writes with. That second one would not fail until a
# diagnostics upload produced an unreadable archive at runtime.
# BOTH directories. zlib's own CMakeLists RENAMES the shipped zconf.h to
# zconf.h.included and generates the real one into the BINARY dir, so the source
# dir alone does not contain the header libarchive needs:
#
#   file(RENAME ${CMAKE_CURRENT_SOURCE_DIR}/zconf.h
#               ${CMAKE_CURRENT_SOURCE_DIR}/zconf.h.included)
#
# Pointing only at the source dir builds anyway on a host that happens to have a
# system zconf.h to fall back on -- macOS does -- and fails on one that does not.
set(ZLIB_INCLUDE_DIR "${ZLIB_SOURCE_DIR};${ZLIB_BINARY_DIR}" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)

# 3.8 or later, deliberately. libarchive 3.7.x declares
# CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12), and CMake 4 removed compatibility
# below 3.5, so it fails to configure at all with
#
#   Compatibility with CMake < 3.5 has been removed from CMake.
#
# 3.8.x declares 3.17 and configures cleanly. Do not "fix" that by passing
# CMAKE_POLICY_VERSION_MINIMUM; take the version that supports the toolchain.
#
# Everything optional is off. The SDK writes gzip + pax (pax needs nothing
# external) and reads with archive_read_support_*_all, which are permissive:
# they enable whatever was compiled in rather than requiring anything. Since the
# only archives it ever reads back are its own, a minimal build loses nothing.
CPMAddPackage(
    NAME LibArchive
    GITHUB_REPOSITORY libarchive/libarchive
    GIT_TAG v3.8.9
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "ENABLE_BZip2 OFF" "ENABLE_LZMA OFF" "ENABLE_ZSTD OFF" "ENABLE_LZ4 OFF"
        "ENABLE_OPENSSL OFF" "ENABLE_LIBXML2 OFF" "ENABLE_EXPAT OFF"
        "ENABLE_ICONV OFF" "ENABLE_ACL OFF" "ENABLE_XATTR OFF" "ENABLE_PCREPOSIX OFF"
        "ENABLE_TAR OFF" "ENABLE_CPIO OFF" "ENABLE_CAT OFF" "ENABLE_UNZIP OFF"
        "ENABLE_TEST OFF" "ENABLE_INSTALL OFF"
        "BUILD_SHARED_LIBS OFF"
)

# The call sites link LibArchive::LibArchive, which is what find_package would
# have given them. An INTERFACE target rather than a bare alias to archive_static,
# because the static libarchive does not carry zlib as a link dependency and the
# consumer would fail to link without it.
# Guarded on OUR target, not on LibArchive::LibArchive. This module really is
# included twice in one configure -- tests/test_detail includes it, and then
# pulls in the SDK root, which includes it again -- so without a guard the second
# pass fails on a duplicate target. Keying that guard to our own name means it
# does not depend on what libarchive does or does not export, now or later.
#
# libarchive exports neither name today: LibArchive::LibArchive comes from
# CMake's own FindLibArchive module, which only runs on the system path, and the
# system path returns above before reaching here.
if(NOT TARGET scorbit_libarchive)
    add_library(scorbit_libarchive INTERFACE)
    target_link_libraries(scorbit_libarchive INTERFACE archive_static zlibstatic)
endif()

# Claim the canonical name only if nothing else has, so a future libarchive that
# does export it wins rather than colliding.
if(NOT TARGET LibArchive::LibArchive)
    add_library(LibArchive::LibArchive ALIAS scorbit_libarchive)
endif()
