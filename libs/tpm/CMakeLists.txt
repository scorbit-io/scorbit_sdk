cmake_minimum_required(VERSION 3.28...4.0)

project(tpm VERSION 1.0.0 LANGUAGES C CXX)

include(${MY_CMAKE_DIR}/guards.cmake)
include(${MY_CMAKE_DIR}/tools.cmake)

# ---- Add dependencies via CPM ----

include(${MY_CMAKE_DIR}/package_project.cmake)

# Add internal dependencies if not already provided by parent project
if(NOT TARGET utils)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../utils ${CMAKE_CURRENT_BINARY_DIR}/utils)
endif()
if(NOT TARGET logger)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../logger ${CMAKE_CURRENT_BINARY_DIR}/logger)
endif()

include(${MY_CMAKE_DIR}/lib_nlohmann_json.cmake)

# cryptoaut
include(lib_cryptoauth.cmake)

find_package(OpenSSL REQUIRED)

# ---- If we should use hardware TPM ----

if(LINUX)
    set(TPM_USE_HW ON)
else()
    set(TPM_USE_HW OFF)
endif()

# ---- Create library ----

include(sources.cmake)
add_library(${PROJECT_NAME} ${headers} ${sources} ${c_sources})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC utils cryptoauth
    PRIVATE logger::logger nlohmann_json::nlohmann_json OpenSSL::SSL OpenSSL::Crypto
)

if(TPM_USE_HW)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TPM_USE_HW)
endif()

# ---- Test ----

if(SCORBIT_AUX_BUILD_TESTS OR (PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR))
    add_subdirectory(test)
endif()
