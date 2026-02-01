cmake_minimum_required(VERSION 3.28...4.0)

# ---- Project ----

project(utils VERSION 1.0.0 LANGUAGES CXX)

# ---- Include guards ----

include(${cmake_dir}/guards.cmake)
include(${cmake_dir}/tools.cmake)

# ---- Add dependencies via CPM ----

include(${cmake_dir}/package_project.cmake)

# CPMAddPackage(
#     NAME cpp-subprocess
#     GITHUB_REPOSITORY arun11299/cpp-subprocess
#     GIT_TAG v2.5
#     GIT_SHALLOW TRUE
#     EXCLUDE_FROM_ALL YES
#     SYSTEM YES
# )

include(${cmake_dir}/lib_fmt.cmake)
include(${cmake_dir}/lib_nlohmann_json.cmake)

CPMAddPackage(NAME logger SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../logger)

# ---- Create library ----

include(sources.cmake)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE ${sources} ${headers})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC nlohmann_json::nlohmann_json
    PUBLIC fmt::fmt logger::logger
    # PRIVATE cpp-subprocess::subprocess  # Uncomment when commandrunner.cpp is enabled
)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)

# ---- Test ----

# add_subdirectory(test)
