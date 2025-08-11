#!/bin/bash

# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

REL=7
SCORBIT_SDK_ABI=u20

set -e

# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

VERSION=$(get_version "$SCRIPT_DIR/../VERSION")

run_build() {
    ARCH=$1
    PLATFORM=$2

    DOCKER_IMAGE=dilshodm/ubuntu-builder-$ARCH:20.04_${REL}
    build_using_docker "$ARCH" "$PLATFORM" "$VERSION" "$SCORBIT_SDK_ABI" "$DOCKER_IMAGE"
}

run_build arm64 linux/arm64
run_build amd64 linux/amd64
run_build arm   linux/arm/v7
