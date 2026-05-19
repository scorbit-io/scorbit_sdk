find_package(CURL 8.0.0)
if(CURL_FOUND)
    set(CPR_OPTIONS "CPR_USE_SYSTEM_CURL ON")
else()
    unset(CURL_LIBRARIES)
endif()

set(cpr_PATCH_FILE "${CMAKE_CURRENT_LIST_DIR}/patches/cpr-gcc-9.3-build-fix.patch")

CPMAddPackage(
    NAME cpr
    URL https://github.com/libcpr/cpr/archive/refs/tags/1.14.2.tar.gz
    URL_HASH SHA256=b9b529b47083bfe80bba855ca5308d12d767ae7c7b629aef5ef018c4343cf62b
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    PATCHES "${cpr_PATCH_FILE}"
    OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_CPR_TESTS OFF" "${CPR_OPTIONS}"
)
