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
# relative path "etc/cryptoauthlib/cryptoauthlib.conf". Force absolute Microchip layout.
if(_SCORBIT_CRYPTOAUTH_PKCS11)
    set(DEFAULT_CONF_PATH "/etc/cryptoauthlib" CACHE STRING "PKCS11 main config directory" FORCE)
    set(DEFAULT_STORE_PATH "/var/lib/cryptoauthlib" CACHE STRING "PKCS11 slot config filestore" FORCE)
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
        "DEFAULT_CONF_PATH /etc/cryptoauthlib"
        "DEFAULT_STORE_PATH /var/lib/cryptoauthlib"
        "DEFAULT_CONF_FILE_NAME cryptoauthlib.conf"
    )
endif()

cpmaddpackage(
    NAME cryptoauth
    URL https://github.com/MicrochipTech/cryptoauthlib/archive/refs/tags/v3.7.9.tar.gz
    URL_HASH SHA256=8923ef8de3371e3d55c03bbdb1b83272e38dc4d674dea5bf5afc1874f7044596
    PATCHES "${CMAKE_CURRENT_LIST_DIR}/cryptoauthlib-build-fixes.patch"
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

    # PKCS11: USB CDC (UART) slot config and Scorbit HID VID/PID (standalone shared build only).
    if(_SCORBIT_CRYPTOAUTH_PKCS11)
        set(_pkcs11_slot_h "${cryptoauth_SOURCE_DIR}/lib/pkcs11/pkcs11_slot.h")
        if(EXISTS "${_pkcs11_slot_h}")
            file(READ "${_pkcs11_slot_h}" _pkcs11_slot_h_content)
            string(REPLACE "uint8_t devpath[24];" "uint8_t devpath[64];" _pkcs11_slot_h_content "${_pkcs11_slot_h_content}")
            file(WRITE "${_pkcs11_slot_h}" "${_pkcs11_slot_h_content}")
        endif()

        set(_pkcs11_config_c "${cryptoauth_SOURCE_DIR}/lib/pkcs11/pkcs11_config.c")
        if(EXISTS "${_pkcs11_config_c}")
            file(READ "${_pkcs11_config_c}" _pkcs11_config_content)
            set(_pkcs11_config_orig "${_pkcs11_config_content}")

            # hid,i2c,c0,vid,pid needs argv[0..4]; parse_interface only had argv[4].
            string(FIND "${_pkcs11_config_content}" "char* argv[6] = { \"\", \"\", \"\", \"\", \"\", \"\" };" _pkcs11_argv6_pos)
            if(_pkcs11_argv6_pos EQUAL -1)
                string(REPLACE
                    "    char* argv[4] = { \"\", \"\", \"\", \"\" };
    CK_RV rv = CKR_GENERAL_ERROR;
    ATCAIfaceCfg * cfg = &slot_ctx->interface_config;"
                    "    char* argv[6] = { \"\", \"\", \"\", \"\", \"\", \"\" };
    CK_RV rv = CKR_GENERAL_ERROR;
    ATCAIfaceCfg * cfg = &slot_ctx->interface_config;"
                    _pkcs11_config_content "${_pkcs11_config_content}")
                string(REPLACE
                    "pkcs11_config_parse_interface(pkcs11_slot_ctx_ptr slot_ctx, char* cfgstr)
{
    int argc = 4;"
                    "pkcs11_config_parse_interface(pkcs11_slot_ctx_ptr slot_ctx, char* cfgstr)
{
    int argc = 6;"
                    _pkcs11_config_content "${_pkcs11_config_content}")
            endif()

            string(FIND "${_pkcs11_config_content}" "strcmp(argv[0], \"uart\")" _pkcs11_uart_patch_pos)

            if(_pkcs11_uart_patch_pos EQUAL -1)
                set(_pkcs11_hid_vidpid_old
"        if (argc > 2)
        {
            errno = 0;
            l_tmp = strtol(argv[2], NULL, 16);
            if ((0 == errno) && (l_tmp > 0) && (l_tmp < PKCS11_CONFIG_U8_MAX))
            {
                ATCA_IFACECFG_VALUE(cfg, atcahid.dev_identity) = (uint8_t)l_tmp;
                rv = CKR_OK;
            }
        }
        else
        {
            rv = CKR_OK;
        }
        #endif
    }
    else if (0 == strcmp(argv[0], \"spi\"))")
                set(_pkcs11_hid_vidpid_new
"        if (argc > 2)
        {
            errno = 0;
            l_tmp = strtol(argv[2], NULL, 16);
            if ((0 == errno) && (l_tmp > 0) && (l_tmp < PKCS11_CONFIG_U8_MAX))
            {
                ATCA_IFACECFG_VALUE(cfg, atcahid.dev_identity) = (uint8_t)l_tmp;
                rv = CKR_OK;
            }
        }
        else
        {
            rv = CKR_OK;
        }

        if ((CKR_OK == rv) && (argc > 4))
        {
            errno = 0;
            l_tmp = strtol(argv[3], NULL, 16);
            if ((0 == errno) && (l_tmp >= 0) && (l_tmp <= 0xFFFF))
            {
                ATCA_IFACECFG_VALUE(cfg, atcahid.vid) = (uint16_t)l_tmp;
            }
            else
            {
                rv = CKR_ARGUMENTS_BAD;
            }

            if (CKR_OK == rv)
            {
                errno = 0;
                l_tmp = strtol(argv[4], NULL, 16);
                if ((0 == errno) && (l_tmp >= 0) && (l_tmp <= 0xFFFF))
                {
                    ATCA_IFACECFG_VALUE(cfg, atcahid.pid) = (uint16_t)l_tmp;
                }
                else
                {
                    rv = CKR_ARGUMENTS_BAD;
                }
            }
        }
        #endif
    }
    else if (0 == strcmp(argv[0], \"uart\"))
    {
#ifdef ATCA_HAL_KIT_UART
        cfg->iface_type = ATCA_UART_IFACE;
        ATCA_IFACECFG_VALUE(cfg, atcauart.dev_interface) = ATCA_KIT_AUTO_IFACE;
        ATCA_IFACECFG_VALUE(cfg, atcauart.dev_identity) = 0xC0;
        ATCA_IFACECFG_VALUE(cfg, atcauart.baud) = 115200;
        ATCA_IFACECFG_VALUE(cfg, atcauart.wordsize) = 8;
        ATCA_IFACECFG_VALUE(cfg, atcauart.parity) = 2;
        ATCA_IFACECFG_VALUE(cfg, atcauart.stopbits) = 1;
        cfg->wake_delay = 1500;
        cfg->rx_retries = 20;
        rv = CKR_ARGUMENTS_BAD;

        if (argc <= 1 || argv[1][0] == '\\0' ||
            (0 == strcmp(argv[1], \"auto\")) || (0 == strcmp(argv[1], \"discover\")))
        {
            if (0 == scorbit_pkcs11_discover_cdc((char*)slot_ctx->devpath, sizeof(slot_ctx->devpath)))
            {
                cfg->cfg_data = (char*)slot_ctx->devpath;
                rv = CKR_OK;
            }
            else
            {
                rv = CKR_DEVICE_ERROR;
            }
        }
        else
        {
            size_t path_len = strlen(argv[1]);
            if (path_len < sizeof(slot_ctx->devpath))
            {
                (void)memcpy(slot_ctx->devpath, argv[1], path_len + 1u);
                cfg->cfg_data = (char*)slot_ctx->devpath;
                rv = CKR_OK;
            }
        }

        if (CKR_OK == rv)
        {
            if (argc > 2)
            {
                errno = 0;
                l_tmp = strtol(argv[2], NULL, 16);
                if ((0 == errno) && (l_tmp > 0) && (l_tmp < PKCS11_CONFIG_U8_MAX))
                {
                    ATCA_IFACECFG_VALUE(cfg, atcauart.dev_identity) = (uint8_t)l_tmp;
                }
                else
                {
                    rv = CKR_ARGUMENTS_BAD;
                }
            }

            if ((CKR_OK == rv) && (argc > 3))
            {
                errno = 0;
                l_tmp = strtol(argv[3], NULL, 10);
                if ((0 == errno) && (l_tmp > 0) && (l_tmp <= PKCS11_CONFIG_U32_MAX))
                {
                    ATCA_IFACECFG_VALUE(cfg, atcauart.baud) = (uint32_t)l_tmp;
                }
                else
                {
                    rv = CKR_ARGUMENTS_BAD;
                }
            }
        }
#else
        rv = CKR_GENERAL_ERROR;
#endif
    }
    else if (0 == strcmp(argv[0], \"spi\"))")
                string(REPLACE "${_pkcs11_hid_vidpid_old}" "${_pkcs11_hid_vidpid_new}" _pkcs11_config_content "${_pkcs11_config_content}")
            endif()

            set(_pkcs11_uart_path_old
"        if ((argc > 1) && (argv[1][0] != '\\0'))
        {
            size_t path_len = strlen(argv[1]);
            if (path_len < sizeof(slot_ctx->devpath))
            {
                (void)memcpy(slot_ctx->devpath, argv[1], path_len + 1u);
                cfg->cfg_data = (char*)slot_ctx->devpath;
                rv = CKR_OK;
            }
        }")
            set(_pkcs11_uart_path_new
"        if (argc <= 1 || argv[1][0] == '\\0' ||
            (0 == strcmp(argv[1], \"auto\")) || (0 == strcmp(argv[1], \"discover\")))
        {
            if (0 == scorbit_pkcs11_discover_cdc((char*)slot_ctx->devpath, sizeof(slot_ctx->devpath)))
            {
                cfg->cfg_data = (char*)slot_ctx->devpath;
                rv = CKR_OK;
            }
            else
            {
                rv = CKR_DEVICE_ERROR;
            }
        }
        else
        {
            size_t path_len = strlen(argv[1]);
            if (path_len < sizeof(slot_ctx->devpath))
            {
                (void)memcpy(slot_ctx->devpath, argv[1], path_len + 1u);
                cfg->cfg_data = (char*)slot_ctx->devpath;
                rv = CKR_OK;
            }
        }")
            string(FIND "${_pkcs11_config_content}" "scorbit_pkcs11_discover_cdc" _pkcs11_discover_fn_pos)
            if(_pkcs11_discover_fn_pos EQUAL -1)
                string(REPLACE "${_pkcs11_uart_path_old}" "${_pkcs11_uart_path_new}" _pkcs11_config_content "${_pkcs11_config_content}")
            endif()

            string(FIND "${_pkcs11_config_content}" "scorbit_pkcs11_discover.h" _pkcs11_discover_inc_pos)
            if(_pkcs11_discover_inc_pos EQUAL -1)
                string(REPLACE
                    "#include \"pkcs11_config.h\""
                    "#include \"pkcs11_config.h\"\n#include \"scorbit_pkcs11_discover.h\""
                    _pkcs11_config_content "${_pkcs11_config_content}")
            endif()

            if(NOT _pkcs11_config_content STREQUAL _pkcs11_config_orig)
                file(WRITE "${_pkcs11_config_c}" "${_pkcs11_config_content}")
            endif()
        endif()

        set(_scorbit_pkcs11_discover_c "${CMAKE_CURRENT_LIST_DIR}/../cryptoauth_shared/scorbit_pkcs11_discover.c")
        if(EXISTS "${_scorbit_pkcs11_discover_c}")
            target_sources(cryptoauth PRIVATE "${_scorbit_pkcs11_discover_c}")
            target_include_directories(cryptoauth PRIVATE
                "${CMAKE_CURRENT_LIST_DIR}/../cryptoauth_shared")
        endif()
    endif()
endif()
