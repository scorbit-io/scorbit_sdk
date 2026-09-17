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

# libarchive needs a zlib for the gzip filter, and this is the part that bites
# quietly: with no zlib it configures happily, leaves HAVE_ZLIB_H undefined and
# builds a libarchive that CANNOT WRITE GZIP -- the one filter this SDK uses.
# Nothing fails until a diagnostics upload produces an unreadable archive.
# Reproduced deliberately: configure with no system zlib and libarchive's
# generated config.h carries "/* #undef HAVE_ZLIB_H */" and exit code 0.
#
# Do NOT add a zlib here. By this point lib_cpr.cmake has configured cpr, which
# fetches zlib-ng (ZLIB_COMPAT) whenever curl needs zlib and the host has none --
# exactly the case this module exists for. Adding a second one collides on the
# shared _deps/zlib-build directory, and forcing ZLIB_LIBRARY at a zlib curl did
# not build leaves curl linking a ZLIB::ZLIB that nothing created.
#
# So: use the system copy if there is one, otherwise the one cpr already brought.
find_package(ZLIB QUIET)

if(ZLIB_FOUND)
    message(STATUS "libarchive: gzip via the system zlib")
    set(_scorbit_zlib_target "")
elseif(TARGET zlibstatic AND DEFINED zlib_BINARY_DIR)
    # zlib-ng in compat mode generates zlib.h and zconf.h into its BINARY dir,
    # not the source tree, so both are needed on the include path.
    message(STATUS "libarchive: gzip via the zlib cpr already fetched (${zlib_BINARY_DIR})")
    set(ZLIB_INCLUDE_DIR "${zlib_BINARY_DIR};${zlib_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
    set(_scorbit_zlib_target zlibstatic)
else()
    # Loud, not silent. Everything above exists because the quiet version of
    # this ships a library that fails at upload time instead of at build time.
    message(FATAL_ERROR
        "libarchive needs a zlib for gzip and none is available: no system zlib, "
        "and cpr did not bring one (no zlibstatic target). Without it libarchive "
        "would build without gzip support and diagnostics archives would be "
        "unreadable, with nothing failing until runtime. See SB-4853.")
endif()

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
    target_link_libraries(scorbit_libarchive INTERFACE archive_static ${_scorbit_zlib_target})
endif()

# Claim the canonical name only if nothing else has, so a future libarchive that
# does export it wins rather than colliding.
if(NOT TARGET LibArchive::LibArchive)
    add_library(LibArchive::LibArchive ALIAS scorbit_libarchive)
endif()
