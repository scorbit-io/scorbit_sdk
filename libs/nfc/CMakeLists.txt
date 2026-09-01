cmake_minimum_required(VERSION 3.28...4.0)

project(nfc VERSION 1.0.0 LANGUAGES CXX)

include(${MY_CMAKE_DIR}/guards.cmake)
include(${MY_CMAKE_DIR}/tools.cmake)

include(${MY_CMAKE_DIR}/package_project.cmake)

include(${MY_CMAKE_DIR}/lib_fmt.cmake)

# ---- Create library ----

include(sources.cmake)
add_library(${PROJECT_NAME} STATIC ${headers} ${sources})

target_compile_definitions(${PROJECT_NAME} PUBLIC USE_FMT_LIBRARY)
target_link_libraries(${PROJECT_NAME} PUBLIC fmt::fmt)

# ListUsbDevices.h falls back to raw libusb probing on Linux when no
# CDC-ACM tty node exists (e.g. cryptoauthlib's libusb CDC transport has
# claimed the interface instead of the kernel driver).
if(LINUX)
    find_library(NFC_LIBUSB_LIBRARY usb-1.0)
    find_path(NFC_LIBUSB_INCLUDE_DIR NAMES libusb.h PATH_SUFFIXES "libusb-1.0" "libusb")

    if(NFC_LIBUSB_LIBRARY AND NFC_LIBUSB_INCLUDE_DIR)
        target_include_directories(${PROJECT_NAME} PUBLIC ${NFC_LIBUSB_INCLUDE_DIR})
        target_link_libraries(${PROJECT_NAME} PUBLIC ${NFC_LIBUSB_LIBRARY})
    else()
        message(WARNING "nfc: libusb not found; libusb CDC probe fallback is disabled")
    endif()
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)

# ---- Create an installable target ----

string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)

packageProject(
    NAME ${PROJECT_NAME}
    VERSION ${PROJECT_VERSION}
    NAMESPACE ${PROJECT_NAME}
    BINARY_DIR ${PROJECT_BINARY_DIR}
    INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include
    INCLUDE_DESTINATION include/${PROJECT_NAME}-${PROJECT_VERSION}
    VERSION_HEADER "${VERSION_HEADER_LOCATION}"
    COMPATIBILITY SameMajorVersion
)
