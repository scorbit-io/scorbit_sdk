cmake_minimum_required(VERSION 3.28...4.0)

project(tpm VERSION 1.0.0 LANGUAGES C CXX)

include(${cmake_dir}/guards.cmake)
include(${cmake_dir}/tools.cmake)

# ---- Add dependencies via CPM ----

include(${cmake_dir}/package_project.cmake)

CPMAddPackage(NAME utils SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../utils)
CPMAddPackage(NAME logger SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../logger)

include(${cmake_dir}/lib_nlohmann_json.cmake)

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

# add_subdirectory(test)
