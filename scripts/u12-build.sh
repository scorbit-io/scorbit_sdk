#!/bin/bash

# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

REL=6

set -e

# Get the directory of the script and source the common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

VERSION=$(get_version "$SCRIPT_DIR/../VERSION")

run_build() {
    ARCH=$1
    PLATFORM=$2
    SCORBIT_SDK_ABI=$3
    RELEASE=$4

    DOCKER_IMAGE=dilshodm/ubuntu-builder-$ARCH:12.04_${RELEASE}
    build_using_docker "$ARCH" "$PLATFORM" "$VERSION" "$SCORBIT_SDK_ABI" "$DOCKER_IMAGE"
}

run_build arm linux/arm/v7 u12 6
run_build arm linux/arm/v7 u12dev 6-devel
