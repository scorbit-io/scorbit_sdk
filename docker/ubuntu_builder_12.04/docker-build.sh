#!/bin/sh

# Determine the directory of the script
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Release build
docker build --platform linux/arm/v7 -t dilshodm/ubuntu-builder-arm:12.04 "$SCRIPT_DIR"

# Development build
docker build --platform linux/arm/v7 \
    --build-arg OPENSSL_CONFIG_ARGS="no-asm" \
    -t dilshodm/ubuntu-builder-arm:12.04-devel "$SCRIPT_DIR"
