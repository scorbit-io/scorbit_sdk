set(SCORBIT_SDK_ENCRYPT_SECRET "$ENV{SCORBIT_SDK_ENCRYPT_SECRET}")

# Read the SCORBIT_SDK_ENCRYPT_SECRET environment variable for production build
if(SCORBIT_SDK_PRODUCTION)
    message(STATUS "Production build")
    string(SHA256 SCORBIT_SDK_HASH "$ENV{SCORBIT_SDK_ENCRYPT_SECRET}")
    if(NOT SCORBIT_SDK_HASH STREQUAL "0f06680dec68958ce55a3c5fa47fc37a138aa30cd6dd52b379277dd81ec5d9dc")
        message(FATAL_ERROR
            "SCORBIT_SDK_ENCRYPT_SECRET is incorrect for production build!"
            " If this is not production build, run cmake with -D SCORBIT_SDK_PRODUCTION=OFF"
        )
    endif()
endif()
