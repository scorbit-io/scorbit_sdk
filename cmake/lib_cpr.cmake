find_package(CURL 8.0.0)
if(CURL_FOUND)
    set(CPR_OPTIONS "CPR_USE_SYSTEM_CURL ON")
else()
    unset(CURL_LIBRARIES)
endif()

CPMAddPackage(
    NAME cpr
    GIT_TAG 1.12.0
    GITHUB_REPOSITORY libcpr/cpr
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    GIT_SHALLOW TRUE
    OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_CPR_TESTS OFF" "${CPR_OPTIONS}"
)

if(cpr_ADDED)
    set(CPR_SOURCE_DIR ${cpr_SOURCE_DIR})

    execute_process(
        COMMAND git apply --check "${CMAKE_CURRENT_SOURCE_DIR}/patches/cpr-gcc-9.3-build-fix.patch"
        WORKING_DIRECTORY ${CPR_SOURCE_DIR}
        RESULT_VARIABLE patch_check_result
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(patch_check_result EQUAL 0)
        message(STATUS "Applying CPR patch...")
        execute_process(
            COMMAND git apply "${CMAKE_CURRENT_SOURCE_DIR}/patches/cpr-gcc-9.3-build-fix.patch"
            WORKING_DIRECTORY ${CPR_SOURCE_DIR}
            RESULT_VARIABLE patch_result
        )
        if(NOT patch_result EQUAL 0)
            message(FATAL_ERROR "Failed to apply patch to CPR")
        endif()
    else()
        message(STATUS "CPR patch already applied (skipping).")
    endif()
endif()
