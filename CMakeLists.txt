cmake_minimum_required(VERSION 3.28)

project(scorbit_sdk_root)

# C++ SDK
add_subdirectory(cpp)

# Python SDK
# Check if Python SDK should be built
option(BUILD_PYTHON_SDK "Build Python SDK" ON)
if(BUILD_PYTHON_SDK)
    # You might need to find Python here
    find_package(Python COMPONENTS Interpreter Development REQUIRED)
    add_subdirectory(python)
endif()

# Documentation
add_subdirectory(docs)

# All (for IDE project structure)
add_subdirectory(all)

# Optional: Add any global settings or options here