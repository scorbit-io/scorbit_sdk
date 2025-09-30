find_package(CURL 8.0.0)
if(CURL_FOUND)
    set(CPR_OPTIONS "CPR_USE_SYSTEM_CURL ON")
else()
    unset(CURL_LIBRARIES)
endif()

set(cpr_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/patches/cpr-gcc-9.3-build-fix.patch")

# Use CPMAddPackages patches option only if CPM_SOURCE_CACHE is defined
# Otherwise it will fail on the second cmake run
if(CPM_SOURCE_CACHE)
    set(cpr_PATCHES_OPTION PATCHES "${cpr_PATCH_FILE}")
endif()

CPMAddPackage(
    NAME cpr
    GIT_TAG 1.12.0
    GITHUB_REPOSITORY libcpr/cpr
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    GIT_SHALLOW TRUE
    ${cpr_PATCHES_OPTION}
    OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_CPR_TESTS OFF" "${CPR_OPTIONS}"
)

# If the patches option is not defined, apply the patch manually
if(NOT DEFINED cpr_PATCHES_OPTION)
    include(patch)
    apply_patch(
        "${CMAKE_CURRENT_SOURCE_DIR}/patches/cpr-gcc-9.3-build-fix.patch"
        "${cpr_SOURCE_DIR}"
    )
endif()
