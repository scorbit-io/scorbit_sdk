function(apply_patch PATCH_FILE SOURCE_DIR)
    # Check if patch can be applied
    execute_process(
        COMMAND git apply --check "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE patch_check_result
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(patch_check_result EQUAL 0)
        message(STATUS "Applying patch: ${PATCH_FILE}")
        execute_process(
            COMMAND git apply "${PATCH_FILE}"
            WORKING_DIRECTORY "${SOURCE_DIR}"
            RESULT_VARIABLE patch_result
        )
        if(NOT patch_result EQUAL 0)
            message(FATAL_ERROR "Failed to apply patch: ${PATCH_FILE}")
        endif()
    else()
        message(STATUS "Patch already applied or not applicable: ${PATCH_FILE}")
    endif()
endfunction()

