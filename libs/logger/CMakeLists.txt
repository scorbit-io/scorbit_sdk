cmake_minimum_required(VERSION 3.28...4.0)

# ---- Project ----

project(logger VERSION 1.0.0 LANGUAGES CXX)

# ---- Include guards ----

include(${cmake_dir}/guards.cmake)
include(${cmake_dir}/tools.cmake)

# ---- Add dependencies via CPM ----

include(${cmake_dir}/lib_fmt.cmake)

# ---- Create library ----

include(sources.cmake)

add_library(${PROJECT_NAME} STATIC)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE ${sources} ${headers})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC fmt::fmt
)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)
