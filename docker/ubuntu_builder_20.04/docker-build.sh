#!/bin/sh

# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

REL=7

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
