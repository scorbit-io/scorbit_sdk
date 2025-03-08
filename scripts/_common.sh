#!/bin/bash

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2025


cleanup_build_files() {
    BUILD_DIR=$1

    # Remove the build directory
    rm $BUILD_DIR/CMakeCache.txt
    rm -rf $BUILD_DIR/_deps/*-build
    rm -rf $BUILD_DIR/encrypt_tool
}

docker_build() {
    CMD=$1
    DOCKER_IMAGE=$2
    PLATFORM=$3

    echo "Build dir is: $BUILD_DIR"

    echo $CMD
    docker container run --rm -it \
        -v $(pwd):/src \
        -v $(pwd)/build/_cache:/cache \
        --workdir /src \
        --user $(id -u):$(id -g) \
        --platform $PLATFORM \
        -e SCORBIT_SDK_ENCRYPT_SECRET \
        -e CPM_SOURCE_CACHE="/cache" \
        $DOCKER_IMAGE bash -c "$CMD"
}

# Print a message in green
print_success() {
    echo -e "\e[32m$1\e[0m"
}

# Print a message in red
print_error() {
    echo -e "\e[31m$1\e[0m"
}

# Get the absolute path of a file
absolute_path() {
    echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
}

# Example usage (only runs when sourced interactively)
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "This script is meant to be sourced, not executed directly."
    exit 1
fi
