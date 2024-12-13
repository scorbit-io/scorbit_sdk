# toolchain.cmake

# Specify the C and C++ compilers
set(CMAKE_SYSTEM_NAME Linux) # Target system is Linux
set(CMAKE_SYSTEM_PROCESSOR aarch64) # Processor architecture

# Path to the toolchain binaries
set(TOOLCHAIN_PATH "/toolchain")
set(CMAKE_SYSROOT "${TOOLCHAIN_PATH}/aarch64-buildroot-linux-gnu/sysroot")

# Specify compilers
set(CMAKE_C_COMPILER "${TOOLCHAIN_PATH}/bin/aarch64-buildroot-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PATH}/bin/aarch64-buildroot-linux-gnu-g++")

# Configure the linker
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
