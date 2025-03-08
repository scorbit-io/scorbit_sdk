#!/bin/bash

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

REL=3

set -e

# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

run_build() {
    ARCH=$1
    PLATFORM=$2
    echo "!!! Building arch: $ARCH, platform: $PLATFORM !!!"

    BUILD_DIR=build/ubuntu-$ARCH-20.04
    DIST_DIR=build/dist/$ARCH
    DOCKER_IMAGE=dilshodm/ubuntu-builder-$ARCH:20.04_${REL}

    CMD="
        cmake \
            -D SCORBIT_SDK_PRODUCTION=ON \
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

run_build arm64 linux/arm64
run_build amd64 linux/amd64
run_build arm   linux/arm/v7
