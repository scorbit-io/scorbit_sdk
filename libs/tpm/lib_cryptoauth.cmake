if(NOT DEFINED SCORBIT_CRYPTOAUTH_BUILD_SHARED)
    set(SCORBIT_CRYPTOAUTH_BUILD_SHARED OFF)
endif()
if(SCORBIT_CRYPTOAUTH_BUILD_SHARED)
    set(_SCORBIT_CRYPTOAUTH_SHARED_LIBS ON)
else()
    set(_SCORBIT_CRYPTOAUTH_SHARED_LIBS OFF)
endif()

# I2C HAL sources are Linux-only (hal_linux_i2c_userspace.c); enabling on macOS breaks the link.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_SCORBIT_CRYPTOAUTH_HAL_I2C ON)
else()
    set(_SCORBIT_CRYPTOAUTH_HAL_I2C OFF)
endif()

# Standalone libcryptoauth.so for pkcs11-provider / p11-kit: export C_GetFunctionList and Cryptoki API.
if(SCORBIT_CRYPTOAUTH_BUILD_SHARED AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_SCORBIT_CRYPTOAUTH_PKCS11 ON)
    set(_SCORBIT_CRYPTOAUTH_OPENSSL ON)
else()
    set(_SCORBIT_CRYPTOAUTH_PKCS11 OFF)
    set(_SCORBIT_CRYPTOAUTH_OPENSSL OFF)
endif()

if(_SCORBIT_CRYPTOAUTH_PKCS11)
    set(_SCORBIT_CRYPTOAUTH_USE_ATCAB_FUNCTIONS ON)
else()
    set(_SCORBIT_CRYPTOAUTH_USE_ATCAB_FUNCTIONS OFF)
endif()

# PKCS11 paths are compile-time strings (ATCA_LIBRARY_CONF / filestore in atca_config.h).
# Cross-builds often set CMAKE_INSTALL_SYSCONFDIR to relative "etc", yielding a broken
# relative path. Force the OpenSSH package layout used by Scorbit installs.
if(_SCORBIT_CRYPTOAUTH_PKCS11)
    set(DEFAULT_CONF_PATH "/usr/local/scorbit/openssh/etc/cryptoauthlib" CACHE STRING "PKCS11 main config directory" FORCE)
    set(DEFAULT_STORE_PATH "/usr/local/scorbit/openssh/etc/cryptoauthlib" CACHE STRING "PKCS11 slot config filestore" FORCE)
    set(DEFAULT_CONF_FILE_NAME "cryptoauthlib.conf" CACHE STRING "PKCS11 main config file name" FORCE)
endif()

set(_cryptoauth_cpm_options
    "ATCA_HAL_I2C ${_SCORBIT_CRYPTOAUTH_HAL_I2C}"
    "ATCA_HAL_KIT_HID ON"
    "ATCA_HAL_KIT_UART ON"
    "ATCA_BUILD_SHARED_LIBS ${_SCORBIT_CRYPTOAUTH_SHARED_LIBS}"
    "ATCA_PKCS11 ${_SCORBIT_CRYPTOAUTH_PKCS11}"
    "ATCA_OPENSSL ${_SCORBIT_CRYPTOAUTH_OPENSSL}"
    "ATCA_USE_ATCAB_FUNCTIONS ${_SCORBIT_CRYPTOAUTH_USE_ATCAB_FUNCTIONS}"
)
if(_SCORBIT_CRYPTOAUTH_PKCS11)
    list(APPEND _cryptoauth_cpm_options
        "DEFAULT_CONF_PATH /usr/local/scorbit/openssh/etc/cryptoauthlib"
        "DEFAULT_STORE_PATH /usr/local/scorbit/openssh/etc/cryptoauthlib"
        "DEFAULT_CONF_FILE_NAME cryptoauthlib.conf"
        "PKCS11_MAX_SLOTS_ALLOWED 2"
    )
endif()

cpmaddpackage(
    NAME cryptoauth
    URL https://github.com/MicrochipTech/cryptoauthlib/archive/refs/tags/v3.7.9.tar.gz
    URL_HASH SHA256=8923ef8de3371e3d55c03bbdb1b83272e38dc4d674dea5bf5afc1874f7044596
    PATCHES
        "${CMAKE_CURRENT_LIST_DIR}/cryptoauthlib-build-fixes.patch"
        "${CMAKE_CURRENT_LIST_DIR}/cryptoauthlib-pkcs11-uart-cdc-interface.patch"
        "${CMAKE_CURRENT_LIST_DIR}/cryptoauthlib-pkcs11-cdc-auto-discovery.patch"
        "${CMAKE_CURRENT_LIST_DIR}/cryptoauthlib-pkcs11-multiple-config-files.patch"
    OPTIONS
        ${_cryptoauth_cpm_options}
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
    # ATECC508A has no on-chip AES command; disable GCM for the SDK static link.
    if(NOT _SCORBIT_CRYPTOAUTH_PKCS11)
        target_compile_definitions(cryptoauth PUBLIC ATCAB_AES_GCM_EN=0)
    else()
        # PKCS11 session types need CBC/CMAC enabled without requiring ATECC608 (LIBRARY_USAGE_EN).
        target_compile_definitions(cryptoauth PRIVATE LIBRARY_USAGE_EN)
        target_compile_definitions(cryptoauth PUBLIC ATCAB_AES_GCM_EN=0)
        # ATECC508A-only build leaves many atcab_* stubs in atca_basic.c with unused parameters.
        target_compile_options(cryptoauth PRIVATE -Wno-unused-parameter)
    endif()

    # Workaournd to export inlclude dirs correctly
    set(_cryptoauth_includes
        ${cryptoauth_BINARY_DIR}/lib
        ${cryptoauth_SOURCE_DIR}/lib
    )
    target_include_directories(cryptoauth PUBLIC ${_cryptoauth_includes})
endif()
