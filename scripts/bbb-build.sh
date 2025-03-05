#!/bin/bash

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

REL=2

PLATFORM=linux/arm/v7

if [ "$1" = "release" ]; then
    BUILD_DIR=build-bbb-release
    DOCKER_IMAGE=dilshodm/ubuntu-builder-arm:12.04_${REL}
elif [ "$1" = "devel" ]; then
    BUILD_DIR=build-bbb-devel
    DOCKER_IMAGE=dilshodm/ubuntu-builder-arm:12.04_${REL}-devel
else
    echo "Usage: $0 [release|devel]"
    exit 1
fi

CMD="
    cmake \
        -D BBB_BUILD=ON \
        -D SCORBIT_SDK_PRODUCTION=ON \
        -G Ninja \
        -B '$BUILD_DIR' \
        -S . \
    && cmake --build '$BUILD_DIR' --config Release \
    && cd '$BUILD_DIR' \
    && cpack -G DEB \
    \
    && cd .. \
    && cmake \
        -D BBB_BUILD=ON \
        -D SCORBIT_SDK_PRODUCTION=ON \
        -G Ninja \
        -B '$BUILD_DIR'/encrypt_tool \
        -S encrypt_tool \
    && cmake --build '$BUILD_DIR'/encrypt_tool --config Release \
    && cd '$BUILD_DIR'/encrypt_tool \
    && cpack -G TGZ
"


# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

cleanup_build_files "$BUILD_DIR"
docker_build "$CMD" "$DOCKER_IMAGE" "$PLATFORM"

