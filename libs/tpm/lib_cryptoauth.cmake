cpmaddpackage(
    NAME cryptoauth
    URL https://github.com/MicrochipTech/cryptoauthlib/archive/refs/tags/v3.7.9.tar.gz
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

    # Patch UART HAL: uart_file[20] is too small for macOS CDC paths
    # (e.g. /dev/cu.usbmodem14101 = 23 chars, buffer only holds 19)
    set(_uart_hal_file "${cryptoauth_SOURCE_DIR}/lib/hal/hal_linux_uart_userspace.c")
    if(EXISTS "${_uart_hal_file}")
        file(READ "${_uart_hal_file}" _uart_hal_content)
        string(FIND "${_uart_hal_content}" "char uart_file[20]" _uart_hal_pos)
        if(NOT _uart_hal_pos EQUAL -1)
            string(REPLACE "char uart_file[20]" "char uart_file[64]" _uart_hal_content "${_uart_hal_content}")
            file(WRITE "${_uart_hal_file}" "${_uart_hal_content}")
        endif()
    endif()

    # UNUSED_VAR is a no-op unless ATCA_UNUSED_VAR_CHECK; fix -Wunused-parameter for strict builds
    set(_atcacert_def "${cryptoauth_SOURCE_DIR}/lib/atcacert/atcacert_def.c")
    if(EXISTS "${_atcacert_def}")
        file(READ "${_atcacert_def}" _atcacert_def_content)
        set(_atcacert_def_orig "${_atcacert_def_content}")
        string(REPLACE
            "    ((void)cert);\n    ((void)cert_size);\n\n    if (NULL != cert_def)"
            "    ((void)cert);\n    ((void)cert_size);\n    ((void)cert_subj_buf);\n\n    if (NULL != cert_def)"
            _atcacert_def_content "${_atcacert_def_content}")
        string(REPLACE
            "    UNUSED_VAR(cert);\n    UNUSED_VAR(cert_size);\n\n    if (NULL != cert_def && NULL != cert_issuer)"
            "    ((void)cert);\n    ((void)cert_size);\n\n    if (NULL != cert_def && NULL != cert_issuer)"
            _atcacert_def_content "${_atcacert_def_content}")
        if(NOT _atcacert_def_content STREQUAL _atcacert_def_orig)
            file(WRITE "${_atcacert_def}" "${_atcacert_def_content}")
        endif()
    endif()
endif()
