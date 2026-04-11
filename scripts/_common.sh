#!/bin/bash

# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

cleanup_build_files() {
    BUILD_DIR=$1

    # Remove the build directory
    # rm $BUILD_DIR/CMakeCache.txt
    # rm -rf $BUILD_DIR/_deps/*-build
    # rm -rf $BUILD_DIR/encrypt_tool
}

docker_build() {
    CMD=$1
    DOCKER_IMAGE=$2

    echo "Build dir is: $BUILD_DIR"

    echo $CMD
    docker container run --rm \
        -v $(pwd):/src \
        -v $(pwd)/build/_cache:/cache \
        --workdir /src \
        --user $(id -u):$(id -g) \
        -e SCORBIT_SDK_ENCRYPT_SECRET \
        -e CPM_SOURCE_CACHE="/cache" \
        -e ARCH=$ARCH \
        $DOCKER_IMAGE bash -c "$CMD"
}

# Same image and volume layout as docker_build, but without a TTY (works under CI / scripts)
# and without ARCH (not needed for pure-Python wheels). Used by python-build.sh / python27-build.sh.
#
# Runs as root (no --user): gcc-builder images often lack python3-venv; upgrading pip
# inside the container for the wheel step requires write access to site-packages.
docker_build_wheel() {
    CMD=$1
    DOCKER_IMAGE=$2

    echo "Wheel build (Docker image: $DOCKER_IMAGE)"
    echo "$CMD"
    docker container run --rm \
        -v "$(pwd):/src" \
        -v "$(pwd)/build/_cache:/cache" \
        --workdir /src \
        -e SCORBIT_SDK_ENCRYPT_SECRET \
        -e CPM_SOURCE_CACHE="/cache" \
        "$DOCKER_IMAGE" bash -lc "$CMD"
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
    VERSION=$2
    ABI=$3
    DOCKER_IMAGE=$4
    echo "!!! Building arch: $ARCH, ABI: $ABI, version: $VERSION !!!"

    BUILD_DIR="build/${ARCH}_${ABI}"
    DIST_DIR=build/dist/$VERSION

    CMD="./scripts/build-core.sh '$BUILD_DIR' '$ABI'"

    cleanup_build_files "$BUILD_DIR" || true
    docker_build "$CMD" "$DOCKER_IMAGE"

    # Move artifacts to dist directory (.deb omitted when cpack DEB was skipped — no dpkg-deb)
    mkdir -p "$DIST_DIR"
    shopt -s nullglob
    artifacts=("$BUILD_DIR"/*.deb "$BUILD_DIR"/*.tar.gz "$BUILD_DIR"/encrypt_tool/*.tar.gz)
    shopt -u nullglob
    if ((${#artifacts[@]})); then
        mv "${artifacts[@]}" "$DIST_DIR"
    fi
}

# Example usage (only runs when sourced interactively)
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "This script is meant to be sourced, not executed directly."
    exit 1
fi
