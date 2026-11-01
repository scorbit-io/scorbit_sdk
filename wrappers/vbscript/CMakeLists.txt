# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# The VBScript binding for Visual Pinball X tables. Configured on its own, like tests/:
#   cmake -S wrappers/vbscript -B build/vbscript
#   ctest --test-dir build/vbscript --output-on-failure
# Writes <build>/scorbit-<version>.vbs, named for the SDK's exact version.

cmake_minimum_required(VERSION 3.28)

project(scorbit_vbscript NONE)

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION SCORBIT_SDK_VERSION LIMIT_COUNT 1)
# A version bump reconfigures, so the file and its tests follow it.
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION
)
string(STRIP "${SCORBIT_SDK_VERSION}" SCORBIT_SDK_VERSION)
set(SCORBIT_VBS ${CMAKE_CURRENT_BINARY_DIR}/scorbit-${SCORBIT_SDK_VERSION}.vbs)

# Only the version is substituted, so the file is the same on every build of a release.
configure_file(scorbit.vbs.in ${SCORBIT_VBS} @ONLY NEWLINE_STYLE CRLF)

enable_testing()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME vbscript_parity
        COMMAND
            ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test/parity.py
            ${CMAKE_CURRENT_SOURCE_DIR}/../../include/scorbit_sdk ${SCORBIT_VBS}
    )
else()
    message(WARNING "No Python 3 interpreter: the VBScript binding's parity test is not registered")
endif()

if(WIN32)
    find_program(CSCRIPT cscript REQUIRED)
    add_test(
        NAME vbscript_binding
        COMMAND ${CSCRIPT} //nologo ${CMAKE_CURRENT_SOURCE_DIR}/test/binding_test.vbs ${SCORBIT_VBS}
    )
endif()
