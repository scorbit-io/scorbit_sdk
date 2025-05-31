#!/bin/sh

REL=6

set -e

# Determine the directory of the script
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

run_build() {
    ARCH=$1
    PLATFORM=$2
    echo "!!! Building arch: $ARCH, platform: $PLATFORM !!!"

    # Release build
    docker build --platform $PLATFORM \
        -t dilshodm/ubuntu-builder-$ARCH:20.04_${REL} \
        "$SCRIPT_DIR"
}

run_build arm64 linux/arm64
run_build amd64 linux/amd64
run_build arm   linux/arm/v7
