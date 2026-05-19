# By Dilshod Mukhtarov, Oct 2025, dilshodm<at>gmail.com

# Usage:
# download_file(<URL> <DEST_DIR> [FILENAME] [EXPECTED_HASH])
# Example:
# download_file("https://example.com/file.txt" "${CMAKE_BINARY_DIR}/downloads")

function(download_file URL DEST_DIR)
    # Optional filename (defaults to last part of URL)
    if(ARGC GREATER 2)
        set(FILENAME "${ARGV2}")
    else()
        get_filename_component(FILENAME "${URL}" NAME)
    endif()
    if(ARGC GREATER 3)
        set(EXPECTED_HASH "${ARGV3}")
    endif()

    # Ensure destination directory exists
    file(MAKE_DIRECTORY "${DEST_DIR}")

    set(OUTPUT_PATH "${DEST_DIR}/${FILENAME}")

    if(EXISTS "${OUTPUT_PATH}" AND DEFINED EXPECTED_HASH)
        if(EXPECTED_HASH MATCHES "^SHA256=([0-9a-fA-F]+)$")
            file(SHA256 "${OUTPUT_PATH}" ACTUAL_HASH)
            string(TOLOWER "${CMAKE_MATCH_1}" EXPECTED_SHA256)
            string(TOLOWER "${ACTUAL_HASH}" ACTUAL_HASH)
            if(NOT ACTUAL_HASH STREQUAL EXPECTED_SHA256)
                message(STATUS "Removing ${OUTPUT_PATH}: SHA256 mismatch")
                file(REMOVE "${OUTPUT_PATH}")
            endif()
        else()
            message(FATAL_ERROR "Unsupported EXPECTED_HASH format: ${EXPECTED_HASH}")
        endif()
    endif()

    # Only download if file does not exist
    if(NOT EXISTS "${OUTPUT_PATH}")
        message(STATUS "Downloading ${URL} -> ${OUTPUT_PATH}")
        set(_download_hash_args "")
        if(DEFINED EXPECTED_HASH)
            list(APPEND _download_hash_args EXPECTED_HASH "${EXPECTED_HASH}")
        endif()

        file(
            DOWNLOAD
            "${URL}"
            "${OUTPUT_PATH}"
            SHOW_PROGRESS
            STATUS STATUS_VAR
            LOG LOG_VAR
            TIMEOUT 60
            ${_download_hash_args}
        )

        list(GET STATUS_VAR 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download ${URL}.\n${LOG_VAR}")
        endif()
    else()
        message(STATUS "File already exists, skipping download: ${OUTPUT_PATH}")
    endif()

    # Return path to downloaded file
    set(DOWNLOADED_FILE "${OUTPUT_PATH}" PARENT_SCOPE)
endfunction()

# Usage:
# download_and_embed_file(<URL> <DEST_DIR> <OUTPUT_HEADER> [RAW_STRING_NAME])
# Example:
# download_and_embed_file("https://curl.se/ca/cacert.pem" "${CMAKE_CURRENT_BINARY_DIR}"
#                         "${CMAKE_CURRENT_BINARY_DIR}/ca_cert_file.h" CACERT_PEM)

function(download_and_embed_file URL DEST_DIR OUTPUT_HEADER)
    if(ARGC GREATER 3)
        set(RAW_STRING_NAME "${ARGV3}")
    else()
        set(RAW_STRING_NAME "FILE_CONTENT")
    endif()

    # Use the existing download_file function
    download_file("${URL}" "${DEST_DIR}")
    set(LOCAL_FILE "${DOWNLOADED_FILE}")

    # Read file contents
    file(READ "${LOCAL_FILE}" FILE_CONTENT)

    # Escape delimiter if necessary
    set(DELIMITER "EOF")
    string(FIND "${FILE_CONTENT}" ")${DELIMITER}\"" DELIM_POS)
    if(NOT DELIM_POS EQUAL -1)
        set(DELIMITER "DATA")
    endif()

    # Generate header with raw string literal
    file(WRITE "${OUTPUT_HEADER}" "// Generated from ${LOCAL_FILE}\n#pragma once\n\n")
    file(APPEND "${OUTPUT_HEADER}" "static const char ${RAW_STRING_NAME}[] = R\"${DELIMITER}(\n")
    file(APPEND "${OUTPUT_HEADER}" "${FILE_CONTENT}")
    file(APPEND "${OUTPUT_HEADER}" ")${DELIMITER}\";\n")

    message(STATUS "Generated C++ header: ${OUTPUT_HEADER}")
endfunction()
