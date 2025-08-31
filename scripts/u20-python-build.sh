#!/bin/bash

# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

SCORBIT_SDK_ABI=u20

set -e

# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Read REL from file DOCKER_RELEASE
REL="$(cat "$SCRIPT_DIR/../DOCKER_RELEASE")"

source "$SCRIPT_DIR/_common.sh"

VERSION=$(get_version "$SCRIPT_DIR/../VERSION")

run_build() {
    ARCH=$1
    PLATFORM=$2
    PYTHON_VER=$3
    echo "!!! Building Python $PYTHON_VER arch: $ARCH, platform: $PLATFORM !!!"

    BUILD_DIR="build/${ARCH}_${SCORBIT_SDK_ABI}_python"
    DIST_DIR=build/dist/$VERSION
    DOCKER_IMAGE=dilshodm/ubuntu-builder-$ARCH:20.04_${REL}

    CMD="
        . /opt/venv$PYTHON_VER/bin/activate \
        && python -m build --wheel \
           --config-setting=build-dir=../../$BUILD_DIR \
           --config-setting=cmake.define.SCORBIT_SDK_ABI=$SCORBIT_SDK_ABI \
           --outdir $DIST_DIR wrappers/python \
        && deactivate \
    "

#    cleanup_build_files "$BUILD_DIR" || true
    docker_build "$CMD" "$DOCKER_IMAGE" "$PLATFORM"

    # Artifacts automatically created at $BUILD_DIR
}

run_build amd64 linux/amd64 3.8
run_build amd64 linux/amd64 3.10
run_build amd64 linux/amd64 3.11

run_build arm64 linux/arm64 3.12

