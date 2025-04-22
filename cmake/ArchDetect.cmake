# By Dilshod Mukhtarov, 2025

# This script is used to detect the target architecture of the compiler
# and normalize it to a standard format.
# TARGET_ARCH is set to: amd64, i386, arm64, armhf

# Function to normalize architecture names
function(normalize_arch raw_arch out_var)
    # Split the triple: arch-vendor-os
    string(REGEX MATCH "^[^-]+" arch_part "${raw_arch}")
    string(TOLOWER "${arch_part}" raw_arch_lc)

    if(raw_arch_lc MATCHES "^(x86_64|x64|amd64)$")
        set(${out_var} "amd64" PARENT_SCOPE)
    elseif(raw_arch_lc MATCHES "^(i[3-6]86|x86)$")
        set(${out_var} "i386" PARENT_SCOPE)
    elseif(raw_arch_lc MATCHES "^(aarch64|arm64|armv8.*|arm64e)$")
        set(${out_var} "arm64" PARENT_SCOPE)
    elseif(raw_arch_lc MATCHES "^(arm|armv7.*|armhf)$")
        set(${out_var} "armhf" PARENT_SCOPE)
    else()
        set(${out_var} "unknown" PARENT_SCOPE)
    endif()
endfunction()

# Detect raw architecture
function(get_target_arch OUT_VAR)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
            OUTPUT_VARIABLE RAW_ARCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER}
            ERROR_VARIABLE RAW_ARCH
            OUTPUT_QUIET
        )

        # Parse from string like "for x64" or "for x86"
        string(REGEX MATCH "for ([^ \n]+)" MATCH_RESULT "${RAW_ARCH}")
        if(MATCH_RESULT)
            set(RAW_ARCH ${CMAKE_MATCH_1})
        else()
            set(RAW_ARCH "unknown")
        endif()
    else()
        set(RAW_ARCH "unknown")
    endif()

    normalize_arch("${RAW_ARCH}" target_arch)
    set(${OUT_VAR} ${target_arch} PARENT_SCOPE)
endfunction()

# Normalize it
get_target_arch(TARGET_ARCH)
message(STATUS "Detected target architecture: ${TARGET_ARCH}")

if(TARGET_ARCH STREQUAL "unknown")
    message(FATAL_ERROR "Unknown target architecture detected.")
endif()
