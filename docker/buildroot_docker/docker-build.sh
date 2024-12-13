#!/bin/sh

# Determine the directory of the script
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

TOOLCHAIN_ARCHIVE="buildroot_toolchain_2024-12-10.tar.gz"
DOCKER_IMAGE=dilshodm/sdk-builder-buildroot-arm64:1

# Check if TOOLCHAIN_ARCHIVE is existed in scripts directory
if [ ! -f "$SCRIPT_DIR/$TOOLCHAIN_ARCHIVE" ]; then
    echo "Please download the $TOOLCHAIN_ARCHIVE file (from gdrive:/Engieering/Development/sdk_support_files/) and place it in this directory: $SCRIPT_DIR/"
    exit 1
fi

# Use the script directory as the Docker build context
docker build --platform linux/amd64 \
    --build-arg TOOLCHAIN_ARCHIVE="$TOOLCHAIN_ARCHIVE" \
    -t "$DOCKER_IMAGE" \
    "$SCRIPT_DIR"
