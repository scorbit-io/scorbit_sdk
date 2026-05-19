find_package(CURL 8.0.0)
if(CURL_FOUND)
    set(CPR_OPTIONS "CPR_USE_SYSTEM_CURL ON")
else()
    unset(CURL_LIBRARIES)
endif()

set(cpr_PATCH_FILE "${CMAKE_CURRENT_LIST_DIR}/patches/cpr-gcc-9.3-build-fix.patch")

CPMAddPackage(
    NAME cpr
    URL https://github.com/libcpr/cpr/archive/refs/tags/1.14.1.tar.gz
    URL_HASH SHA256=213ccc7c98683d2ca6304d9760005effa12ec51d664bababf114566cb2b1e23c
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    PATCHES "${cpr_PATCH_FILE}"
    OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_CPR_TESTS OFF" "${CPR_OPTIONS}"
)
