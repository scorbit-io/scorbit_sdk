#!/bin/bash

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

SCORBIT_SDK_ABI=macos

set -e

# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

VERSION=$(get_version "$SCRIPT_DIR/../VERSION")

run_build() {
    ARCH=$1

    echo "!!! Building arch: $ARCH !!!"

    BUILD_DIR="build/${ARCH}_${SCORBIT_SDK_ABI}"
    DIST_DIR=build/dist/$VERSION

    CMD_TGZ="
        cmake \
            -D SCORBIT_SDK_PRODUCTION=ON \
            -D SCORBIT_SDK_ABI='$SCORBIT_SDK_ABI' \
            -G Ninja  \
            -B '$BUILD_DIR' \
            -S . \
        && cmake --build '$BUILD_DIR' --config Release \
        && pushd '$BUILD_DIR' \
        && cpack -G TGZ \
        && popd
    "

    CMD_WHL="
        eval "$(conda "shell.$(basename "${SHELL}")" hook)" \
        && conda activate \
        && python -m build --wheel \
           --config-setting=build-dir=../../$BUILD_DIR \
           --config-setting=cmake.define.SCORBIT_SDK_ABI=$SCORBIT_SDK_ABI \
           --config-setting=cmake.build-type=Release \
           --outdir $DIST_DIR wrappers/python \
        && conda deactivate \
    "

    #cleanup_build_files "$BUILD_DIR" || true
    #bash -c "$CMD_TGZ"
    bash -c "$CMD_WHL"

    # Move artifacts to dist directory
    mkdir -p "$DIST_DIR"
    mv $BUILD_DIR/*.deb $BUILD_DIR/*.tar.gz $BUILD_DIR/encrypt_tool/*.tar.gz $DIST_DIR
}

run_build arm64
