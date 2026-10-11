# All three patches are skipped under MSVC, for the reason lib_cpr.cmake documents:
# applying a patch there does not merely add nothing, it FAILS THE BUILD. The
# patch.exe CMake finds on a Windows host is commonly GNU patch 2.5.9, shipped
# inside Strawberry Perl, which aborts on a diff with
#
#   Assertation failed! ... patch.c, Line 354 ... Expression: hunk
#
# taking the configure step with it before a single source file is compiled.
#
# Skipping them there is correct rather than merely expedient, because all three
# are no-ops on Windows:
#
#   cryptoauthlib-libusb-cdc.patch  touches lib/CMakeLists.txt and the
#     hal_linux_* sources. Every functional block it adds to lib/CMakeLists.txt
#     is wrapped in `if(LINUX AND ATCA_HAL_KIT_UART AND ATCA_HAL_KIT_UART_LIBUSB)`;
#     the only unguarded line declares an option nothing then reads. The HAL
#     sources are compiled only under cryptoauthlib's LINUX / LINUX OR APPLE
#     branches.
#   cryptoauthlib-build-fixes.patch touches atcacert_def.c and
#     hal_linux_uart_userspace.c. The atcacert_def.c hunk is unused-variable
#     suppression only -- ((void)x) casts -- with no behavioural change.
#   cryptoauthlib-tappad-lock.patch adds hal_tappad_lock.c inside the same
#     `elseif(LINUX OR APPLE)` branch that compiles hal_linux_uart_userspace.c,
#     and defines ATCA_HAL_TAPPAD_LOCK only when it did. It must be applied
#     after cryptoauthlib-libusb-cdc.patch, whose lines it uses as context.
#     See README-tappad-lock.md.
#
# Confirmed rather than reasoned: the SDK's own Windows job built
# cryptoauth.lib with neither patch applied, because it goes through vcpkg,
# which patches its ports internally and never shells out to GNU patch.
#
# NOT `NOT WIN32` and NOT `LINUX` only: hal_linux_uart_userspace.c is also
# compiled on macOS through cryptoauthlib's `elseif(LINUX OR APPLE)` branch, so
# Apple must keep all three patches.
#
# The whole PATCHES list is dropped rather than set empty: CPM would otherwise
# receive an empty argument where it expects a path. See SB-4883, and SB-4852
# which established this failure for cpr.
set(cryptoauth_PATCH_ARGS)
if(NOT MSVC)
    set(cryptoauth_PATCH_ARGS PATCHES
        "${CMAKE_CURRENT_LIST_DIR}/cryptoauthlib-build-fixes.patch"
        "${CMAKE_CURRENT_LIST_DIR}/cryptoauthlib-libusb-cdc.patch"
        "${CMAKE_CURRENT_LIST_DIR}/cryptoauthlib-tappad-lock.patch")
endif()

cpmaddpackage(
    NAME cryptoauth
    URL https://github.com/MicrochipTech/cryptoauthlib/archive/refs/tags/v3.7.9.tar.gz
    URL_HASH SHA256=8923ef8de3371e3d55c03bbdb1b83272e38dc4d674dea5bf5afc1874f7044596
    ${cryptoauth_PATCH_ARGS}
    OPTIONS
        "ATCA_HAL_I2C ON"
        "ATCA_HAL_KIT_HID ON"
        "ATCA_HAL_KIT_UART ON"
        "ATCA_HAL_KIT_UART_LIBUSB ON"
        "ATCA_BUILD_SHARED_LIBS OFF"
        "ATCA_ATECC508A_SUPPORT ON"
        "ATCA_ATSHA204A_SUPPORT OFF"
        "ATCA_ATSHA206A_SUPPORT OFF"
        "ATCA_ATECC108A_SUPPORT OFF"
        "ATCA_ATECC608_SUPPORT OFF"
        "ATCA_ECC204_SUPPORT OFF"
        "ATCA_TA010_SUPPORT OFF"
        "ATCA_SHA104_SUPPORT OFF"
        "ATCA_SHA105_SUPPORT OFF"
        "ATCA_TA_SUPPORT OFF"
)

if(cryptoauth_ADDED)
    # Workaournd to export AES GCM support disable
    target_compile_definitions(cryptoauth PUBLIC ATCAB_AES_GCM_EN=0)

    # Workaournd to export inlclude dirs correctly
    set(_cryptoauth_includes
        ${cryptoauth_BINARY_DIR}/lib
        ${cryptoauth_SOURCE_DIR}/lib
    )
    target_include_directories(cryptoauth PUBLIC ${_cryptoauth_includes})

    # hal_tappad_lock.c has a static __thread; built as PIC it uses the dlopen-safe local-dynamic TLS
    # model in whatever shared library links this archive (e.g. one loaded through Python ctypes).
    # tools.cmake already sets PIC globally for the top-level build; this is defensive.
    set_target_properties(cryptoauth PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # The SDK's TappadLock calls hal_tappad_lock() only when the patch actually compiled it, which
    # the patch signals with ATCA_HAL_TAPPAD_LOCK; elsewhere (MSVC, a configuration without the
    # kit-UART HAL, or a cryptoauthlib without the patch) TappadLock is a no-op.
    get_target_property(_cryptoauth_defs cryptoauth INTERFACE_COMPILE_DEFINITIONS)
    if(_cryptoauth_defs AND "ATCA_HAL_TAPPAD_LOCK" IN_LIST _cryptoauth_defs)
        target_compile_definitions(cryptoauth PUBLIC SCORBIT_TAPPAD_LOCK=1)
        # Set only by tests/CMakeLists.txt, so test processes can be kept off the real lock file.
        if(SCORBIT_TAPPAD_LOCK_TEST_ENV)
            target_compile_definitions(cryptoauth PRIVATE ATCA_TAPPAD_LOCK_ENV_OVERRIDE)
        endif()
    endif()
endif()
