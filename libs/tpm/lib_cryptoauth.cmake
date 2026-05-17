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

cpmaddpackage(
    NAME cryptoauth
    URL https://github.com/MicrochipTech/cryptoauthlib/archive/refs/tags/v3.7.9.tar.gz
    URL_HASH SHA256=8923ef8de3371e3d55c03bbdb1b83272e38dc4d674dea5bf5afc1874f7044596
    PATCHES "${CMAKE_CURRENT_LIST_DIR}/cryptoauthlib-build-fixes.patch"
    OPTIONS
        "ATCA_HAL_I2C ${_SCORBIT_CRYPTOAUTH_HAL_I2C}"
        "ATCA_HAL_KIT_HID ON"
        "ATCA_HAL_KIT_UART ON"
        "ATCA_BUILD_SHARED_LIBS ${_SCORBIT_CRYPTOAUTH_SHARED_LIBS}"
        "ATCA_PKCS11 ${_SCORBIT_CRYPTOAUTH_PKCS11}"
        "ATCA_OPENSSL ${_SCORBIT_CRYPTOAUTH_OPENSSL}"
        "ATCA_USE_ATCAB_FUNCTIONS ${_SCORBIT_CRYPTOAUTH_USE_ATCAB_FUNCTIONS}"
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
    endif()

    # Workaournd to export inlclude dirs correctly
    set(_cryptoauth_includes
        ${cryptoauth_BINARY_DIR}/lib
        ${cryptoauth_SOURCE_DIR}/lib
    )
    target_include_directories(cryptoauth PUBLIC ${_cryptoauth_includes})
endif()
