# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# IMPORTNANT: Add this if you want to install nlohmann_json for downstream users:
# install_nlohmann_json()

# Author: Dilshod Mukhtarov, 2025

include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

set(TRY_NLOHMANN_JSON_VERSION 3.12.0) # Set the desired version here for fetching by CPM

# Don't use find_package if package was added by CPM, otherwise can't find include files
if(NOT NLOHMANN_JSON_ADDED_BY_CPM)
    find_package(nlohmann_json QUIET)
endif()

if(nlohmann_json_FOUND)
    set(NLOHMANN_JSON_ADDED_BY_CPM OFF)
    message(STATUS "Using system nlohmann_json version ${nlohmann_json_VERSION}")
else()
    set(NLOHMANN_JSON_ADDED_BY_CPM ON)
    message(STATUS "System nlohmann-json not found, fetching version ${TRY_NLOHMANN_JSON_VERSION} using CPM.")

    # CPMAddPackage(NAME nlohmann_json URL https://github.com/nlohmann/json/releases/download/v${TRY_NLOHMANN_JSON_VERSION}/json.tar.xz)
    # Fetch with CPM but don't add to build
     CPMAddPackage(
         NAME nlohmann_json
         URL https://github.com/nlohmann/json/releases/download/v${TRY_NLOHMANN_JSON_VERSION}/json.tar.xz
         DOWNLOAD_ONLY YES  # Only download, don't add to build
     )

     # Create an IMPORTED target manually (like find_package would do)
     if(NOT TARGET nlohmann_json::nlohmann_json)
         add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED GLOBAL)

         # Set the include directory - make sure the path is correct
         if(EXISTS "${nlohmann_json_SOURCE_DIR}/include/nlohmann/json.hpp")
             set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
                 INTERFACE_INCLUDE_DIRECTORIES "${nlohmann_json_SOURCE_DIR}/include"
             )
             message(STATUS "Created IMPORTED target nlohmann_json::nlohmann_json with include dir: ${nlohmann_json_SOURCE_DIR}/include")
         else()
             message(FATAL_ERROR "nlohmann_json header not found at expected location: ${nlohmann_json_SOURCE_DIR}/include/nlohmann/json.hpp")
         endif()
     endif()
endif()

# Function to install nlohmann_json when fetched via CPM
function(install_nlohmann_json)
    if(NLOHMANN_JSON_ADDED_BY_CPM)
        # Install the headers
        install(
            DIRECTORY "${nlohmann_json_SOURCE_DIR}/include/"
            DESTINATION include
            FILES_MATCHING PATTERN "*.hpp"
        )

        # Generate a proper config file for downstream consumers
        set(NLOHMANN_JSON_CONFIG_CONTENT "
# Auto-generated config file for nlohmann_json (installed via CPM)
if(NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES \"\${CMAKE_CURRENT_LIST_DIR}/../../../include\"
    )
endif()

set(nlohmann_json_FOUND TRUE)
set(nlohmann_json_VERSION \"${TRY_NLOHMANN_JSON_VERSION}\")
")

        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/nlohmann_json-config.cmake"
            "${NLOHMANN_JSON_CONFIG_CONTENT}"
        )

        install(
            FILES "${CMAKE_CURRENT_BINARY_DIR}/nlohmann_json-config.cmake"
            DESTINATION lib/cmake/nlohmann_json
        )

        message(STATUS "nlohmann_json headers and config will be installed")
    endif()
endfunction()
