# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

set(SCORBIT_SDK_ENCRYPT_SECRET "$ENV{SCORBIT_SDK_ENCRYPT_SECRET}")

set(SCORBIT_SDK_PRODUCTION_KEY_HASH
    "0f06680dec68958ce55a3c5fa47fc37a138aa30cd6dd52b379277dd81ec5d9dc"
)

# Read the SCORBIT_SDK_ENCRYPT_SECRET environment variable for production build
if(SCORBIT_SDK_PRODUCTION)
    message(STATUS "Production build")
    string(SHA256 SCORBIT_SDK_THIS_KEY_HASH "$ENV{SCORBIT_SDK_ENCRYPT_SECRET}")
    if(NOT SCORBIT_SDK_THIS_KEY_HASH STREQUAL SCORBIT_SDK_PRODUCTION_KEY_HASH)
        message(
            FATAL_ERROR
            "SCORBIT_SDK_ENCRYPT_SECRET [$ENV{SCORBIT_SDK_ENCRYPT_SECRET}] is incorrect for production build!"
            " If this is not production build, run cmake with -D SCORBIT_SDK_PRODUCTION=OFF"
        )
    endif()
endif()
