#!/bin/bash

# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

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

# Read version from VERSION file
get_version() {
    FILENAME=$1
    local v=$(<$FILENAME)
    v="${v//[$'\t\r\n ']}"
    echo "$v"
}

build_using_docker() {
    ARCH=$1
    PLATFORM=$2
    VERSION=$3
    ABI=$4
    DOCKER_IMAGE=$5
    echo "!!! Building arch: $ARCH, platform: $PLATFORM, ABI: $ABI, version: $VERSION !!!"

    BUILD_DIR="build/${ARCH}_${ABI}"
    DIST_DIR=build/dist/$VERSION

    CMD="
        cmake \
            -D SCORBIT_SDK_PRODUCTION=ON \
            -D SCORBIT_SDK_ABI='$SCORBIT_SDK_ABI' \
            -G Ninja  \
            -B '$BUILD_DIR' \
            -S . \
        && cmake --build '$BUILD_DIR' --config Release \
        && pushd '$BUILD_DIR' \
        && cpack -G DEB \
        && cpack -G TGZ \
        && popd \
        \
        && cmake \
            -D SCORBIT_SDK_PRODUCTION=ON \
            -G Ninja \
            -B '$BUILD_DIR/encrypt_tool' \
            -S encrypt_tool \
        && cmake --build '$BUILD_DIR/encrypt_tool' --config Release \
        && pushd '$BUILD_DIR/encrypt_tool' \
        && cpack -G TGZ \
        && popd
    "

    cleanup_build_files "$BUILD_DIR" || true
    docker_build "$CMD" "$DOCKER_IMAGE" "$PLATFORM"

    # Move artifacts to dist directory
    mkdir -p "$DIST_DIR"
    mv $BUILD_DIR/*.deb $BUILD_DIR/*.tar.gz $BUILD_DIR/encrypt_tool/*.tar.gz $DIST_DIR
}

# Example usage (only runs when sourced interactively)
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "This script is meant to be sourced, not executed directly."
    exit 1
fi
