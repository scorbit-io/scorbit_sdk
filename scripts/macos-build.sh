#!/bin/bash

# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

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

    CMD="$SCRIPT_DIR/build-core.sh '$BUILD_DIR' '$SCORBIT_SDK_ABI'"

    #cleanup_build_files "$BUILD_DIR" || true
    # bash -c "$CMD_TGZ"
    # bash -c "$CMD_WHL"
    bash -c "$CMD"

    # Move artifacts to dist directory (no .deb on macOS when DEB cpack is skipped)
    mkdir -p "$DIST_DIR"
    shopt -s nullglob
    artifacts=("$BUILD_DIR"/*.deb "$BUILD_DIR"/*.tar.gz "$BUILD_DIR"/encrypt_tool/*.tar.gz)
    shopt -u nullglob
    if ((${#artifacts[@]})); then
        mv "${artifacts[@]}" "$DIST_DIR"
    fi
}

run_build arm64
