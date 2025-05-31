#!/bin/sh

REL=6

# Determine the directory of the script
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Release build
echo "!!! Building Ubuntu 12.04 release !!!"
docker build --platform linux/arm/v7 -t dilshodm/ubuntu-builder-arm:12.04_${REL} "$SCRIPT_DIR"

# Development build
echo "!!! Building Ubuntu 12.04 devel !!!"
docker build --platform linux/arm/v7 \
    --build-arg OPENSSL_CONFIG_ARGS="no-asm" \
    -t dilshodm/ubuntu-builder-arm:12.04_${REL}-devel "$SCRIPT_DIR"
