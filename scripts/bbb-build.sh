#!/bin/bash

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

REL=4
SCORBIT_SDK_ABI=u12

set -e

# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

VERSION=$(get_version "$SCRIPT_DIR/../VERSION")

run_build() {
    ARCH=$1
    PLATFORM=$2

    DOCKER_IMAGE=dilshodm/ubuntu-builder-$ARCH:12.04_${REL}-devel
    build_using_docker "$ARCH" "$PLATFORM" "$VERSION" "$SCORBIT_SDK_ABI" "$DOCKER_IMAGE"
}

run_build arm linux/arm/v7
