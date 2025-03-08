#!/bin/bash

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

REL=1

BUILD_DIR=build/buildroot-release
DIST_DIR=build/dist/buildroot
DOCKER_IMAGE=dilshodm/sdk-builder-buildroot-arm64:${REL}
PLATFORM=linux/amd64

CMD="
    cmake \
        -D CMAKE_TOOLCHAIN_FILE=/toolchain/toolchain.cmake \
        -D SCORBIT_SDK_INSTALL_PREFIX=/usr/lib \
        -D SCORBIT_SDK_PRODUCTION=ON \
        -G Ninja \
        -B '$BUILD_DIR' \
        -S . \
    && cmake --build '$BUILD_DIR' --config Release \
    && pushd '$BUILD_DIR' \
    && cpack -G STGZ \
    && cpack -G TGZ \
    && popd
"

# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

cleanup_build_files "$BUILD_DIR"
docker_build "$CMD" "$DOCKER_IMAGE" "$PLATFORM"

# Move artifacts to dist directory
mkdir -p "$DIST_DIR"
mv $BUILD_DIR/*.sh $BUILD_DIR/*.tar.gz $DIST_DIR
