#!/bin/sh

# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

set -e

# Determine the directory of the script
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Read REL from file DOCKER_RELEASE
REL="$(cat "$SCRIPT_DIR/../../DOCKER_RELEASE")"

# Release build
echo "!!! Building Ubuntu 12.04 !!!"
docker build --platform linux/arm/v7 \
    --build-arg OPENSSL_CONFIG_ARGS="no-asm" \
    -t dilshodm/ubuntu-builder-arm:12.04_${REL} "$SCRIPT_DIR"
