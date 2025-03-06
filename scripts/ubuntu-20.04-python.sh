#!/bin/bash

# Dilshod Mukhtarov <dilshodm@gmail.com>, Mar 2025

REL=3

set -e

# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

run_build() {
    ARCH=$1
    PLATFORM=$2
    PYTHON_VER=$3
    echo "!!! Building Python $PYTHON_VER arch: $ARCH, platform: $PLATFORM !!!"

    BUILD_DIR=build/ubuntu-$ARCH-20.04-python
    DIST_DIR=build/dist/$ARCH
    DOCKER_IMAGE=dilshodm/ubuntu-builder-$ARCH:20.04_${REL}

    CMD="
        . /opt/venv$PYTHON_VER/bin/activate \
        && python -m build --wheel --config-setting=build-dir=../../$BUILD_DIR \
           --outdir $DIST_DIR wrappers/python \
        && deactivate \
    "

    cleanup_build_files "$BUILD_DIR" || true
    docker_build "$CMD" "$DOCKER_IMAGE" "$PLATFORM"

    # Move artifacts to dist directory
}

run_build amd64 linux/amd64 3.8
run_build amd64 linux/amd64 3.10

#run_build arm64 linux/arm64 3.8

#run_build arm   linux/arm/v7 3.10
