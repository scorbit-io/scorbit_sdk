cpmaddpackage(
    NAME cryptoauth
    URL https://github.com/MicrochipTech/cryptoauthlib/archive/refs/tags/v3.7.9.tar.gz
    URL_HASH SHA256=8923ef8de3371e3d55c03bbdb1b83272e38dc4d674dea5bf5afc1874f7044596
    PATCHES "${CMAKE_CURRENT_LIST_DIR}/cryptoauthlib-build-fixes.patch"
    OPTIONS
        "ATCA_HAL_I2C ON"
        "ATCA_HAL_KIT_HID ON"
        "ATCA_HAL_KIT_UART ON"
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
endif()
