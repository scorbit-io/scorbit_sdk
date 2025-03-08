cmake_minimum_required(VERSION 3.28)

# project(scorbit_python LANGUAGES CXX)

project(${SKBUILD_PROJECT_NAME} VERSION ${SKBUILD_PROJECT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Import tools ----

include(../../cmake/tools.cmake)

# find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)

CPMAddPackage(
    NAME pybind11
    URL https://github.com/pybind/pybind11/archive/d8565ac7317b4838a08308170f5b9ba82d091546.tar.gz
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    GIT_SHALLOW TRUE
    OPTIONS "PYBIND11_FINDPYTHON ON"
)

CPMAddPackage(NAME scorbit_sdk SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../.. EXCLUDE_FROM_ALL YES)
# find_package(scorbit_sdk REQUIRED)

pybind11_add_module(scorbit src/game_state_py.cpp)

# Set RPATH
if(APPLE)
    set_target_properties(scorbit PROPERTIES INSTALL_RPATH "@loader_path")
else()
    set_target_properties(scorbit PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

target_link_libraries(scorbit PRIVATE scorbit_sdk::scorbit_sdk)

# Check if pybind11-stubgen is available
execute_process(
    COMMAND ${Python_EXECUTABLE} -m pybind11_stubgen --help
    RESULT_VARIABLE STUBGEN_EXIT_CODE
    OUTPUT_QUIET ERROR_QUIET
)

# Only add the command if pybind11-stubgen is available
if(STUBGEN_EXIT_CODE EQUAL 0)
    add_custom_command(
        TARGET scorbit POST_BUILD
        COMMAND ${Python_EXECUTABLE} -m pybind11_stubgen scorbit -o ${CMAKE_CURRENT_BINARY_DIR}/stubs
        COMMENT "Generating stubs using pybind11-stubgen"
    )
    # Install the generated stubs
    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/stubs/ DESTINATION scorbit FILES_MATCHING PATTERN "*")
else()
    message(WARNING "pybind11-stubgen not found, skipping stub generation.")
endif()

install(TARGETS scorbit DESTINATION scorbit)
install(
    TARGETS scorbit_sdk
    LIBRARY DESTINATION scorbit
    ARCHIVE DESTINATION scorbit
    RUNTIME DESTINATION scorbit
)

