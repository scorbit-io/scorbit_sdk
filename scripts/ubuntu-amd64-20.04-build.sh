#!/bin/bash

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

REL=2

BUILD_DIR=build/ubuntu-amd64-20.04
DOCKER_IMAGE=dilshodm/ubuntu-builder-amd64:20.04_${REL}
PLATFORM=linux/amd64

CMD="
    cmake \
        -D UBUNTU_BUILD=ON \
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
        -D UBUNTU_BUILD=ON \
        -D SCORBIT_SDK_PRODUCTION=ON \
        -G Ninja \
        -B '$BUILD_DIR/encrypt_tool' \
        -S encrypt_tool \
    && cmake --build '$BUILD_DIR/encrypt_tool' --config Release \
    && pushd '$BUILD_DIR/encrypt_tool' \
    && cpack -G TGZ \
    && popd
"

# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

cleanup_build_files "$BUILD_DIR"
docker_build "$CMD" "$DOCKER_IMAGE" "$PLATFORM"
