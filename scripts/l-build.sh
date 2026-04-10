#!/bin/bash

# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

set -e

# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$SCRIPT_DIR/.."

# Read REL from file DOCKER_RELEASE
REL="$(cat "$SCRIPT_DIR/../DOCKER_RELEASE")"

source "$SCRIPT_DIR/_common.sh"

VERSION=$(get_version "$SCRIPT_DIR/../VERSION")

run_build() {
    ARCH=$1

    if [ "$ARCH" == "armhf" ]; then
        SCORBIT_SDK_ABI="u12"
    elif [ "$ARCH" == "arm64" ]; then
        SCORBIT_SDK_ABI="u20"
    elif [ "$ARCH" == "amd64" ]; then
        SCORBIT_SDK_ABI="u20"
    fi

    DOCKER_IMAGE=dilshodm/gcc-builder-$ARCH:${REL}
    build_using_docker "$ARCH" "$VERSION" "$SCORBIT_SDK_ABI" "$DOCKER_IMAGE"
}

if [ -z "$1" ]; then
    echo "Usage: $0 <arch>"
    echo "Arch: armhf, arm64, amd64"
    exit 1
fi

run_build $1
