find_package(CURL 8.0.0)
set(CPR_OPTIONS)
if(CURL_FOUND)
    list(APPEND CPR_OPTIONS "CPR_USE_SYSTEM_CURL ON")
else()
    unset(CURL_LIBRARIES)

    # No system curl, so cpr builds its own. Since curl 8.13 that drags in
    # libpsl, and cpr only takes a system libpsl when it is also taking a system
    # curl -- so on this path it tries to BUILD libpsl, which needs meson:
    #
    #   meson not found. ... Meson is required for building libpsl for curl on
    #   Windows.
    #
    # Disabled HERE ONLY, not globally. cpr calls libpsl a secure default and it
    # is genuinely in force wherever a system curl is found, which is every host
    # that builds today. Turning it off everywhere to satisfy the one host that
    # cannot build it would weaken the platforms that can.
    #
    # What is lost on this path is curl's public-suffix-list awareness for cookie
    # domain policy. This SDK talks to one known API rather than acting as a
    # general-purpose browser-like client, so the exposure is small -- but it is
    # a real reduction and is recorded here rather than left to be discovered.
    list(APPEND CPR_OPTIONS "CPR_CURL_USE_LIBPSL OFF")
endif()

# The patch adds allocator member typedefs (size_type, pointer, rebind, ...) that
# C++20 removed from std::allocator and that gcc 9.3's libstdc++ did not supply
# through std::allocator_traits. A conforming implementation derives them from
# value_type/allocate/deallocate, so MSVC's STL needs none of them.
#
# Skipped under MSVC because applying it there does not merely add nothing, it
# FAILS THE BUILD. The patch.exe CMake finds on a Windows host is commonly GNU
# patch 2.5.9, shipped inside Strawberry Perl, which aborts on this diff with
#
#   Assertation failed! ... patch.c, Line 354 ... Expression: hunk
#
# taking the configure step with it before a single source file is compiled.
#
# The whole PATCHES pair is dropped rather than set empty: CPM would otherwise
# receive an empty argument where it expects a path. Narrow on purpose -- every
# toolchain that applies this patch today still applies it.
set(cpr_PATCH_ARGS)
if(NOT MSVC)
    set(cpr_PATCH_ARGS PATCHES
        "${CMAKE_CURRENT_LIST_DIR}/patches/cpr-gcc-9.3-build-fix.patch")
endif()

CPMAddPackage(
    NAME cpr
    URL https://github.com/libcpr/cpr/archive/refs/tags/1.14.2.tar.gz
    URL_HASH SHA256=b9b529b47083bfe80bba855ca5308d12d767ae7c7b629aef5ef018c4343cf62b
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    ${cpr_PATCH_ARGS}
    OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_CPR_TESTS OFF" ${CPR_OPTIONS}
)
