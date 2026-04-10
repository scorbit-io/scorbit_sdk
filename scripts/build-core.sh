#!/bin/bash

# Scorbit SDK
#
# (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License

set -e

BUILD_DIR=$1
ABI=$2

if [[ -z "$BUILD_DIR" || -z "$ABI" ]]; then
    echo "Usage: build-core.sh <BUILD_DIR> <ABI>"
    exit 1
fi

# Pass through when set in the environment (e.g. ENV in gcc-builder Docker image).
# Do not use \$ here — that sent the literal "$CMAKE_TOOLCHAIN_FILE" to CMake.
CMAKE_TOOLCHAIN_ARGS=()
if [[ -n "${CMAKE_TOOLCHAIN_FILE}" ]]; then
    CMAKE_TOOLCHAIN_ARGS=(-D "CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
fi

cmake \
    "${CMAKE_TOOLCHAIN_ARGS[@]}" \
    -D SCORBIT_SDK_PRODUCTION=ON \
    -D SCORBIT_SDK_ABI="$ABI" \
    -D CMAKE_BUILD_TYPE=Release \
    -G Ninja \
    -B "$BUILD_DIR" \
    -S . \
&& cmake --build "$BUILD_DIR" \
&& { pushd "$BUILD_DIR" \
    && { if command -v dpkg-deb >/dev/null 2>&1; then cpack -G DEB; else echo "Skipping cpack DEB (dpkg-deb not found)"; fi; } \
    && cpack -G TGZ \
    && popd; } \
\
&& cmake \
    "${CMAKE_TOOLCHAIN_ARGS[@]}" \
    -D SCORBIT_SDK_PRODUCTION=ON \
    -D CMAKE_BUILD_TYPE=Release \
    -G Ninja \
    -B "$BUILD_DIR/encrypt_tool" \
    -S encrypt_tool \
&& cmake --build "$BUILD_DIR/encrypt_tool" \
&& pushd "$BUILD_DIR/encrypt_tool" \
&& cpack -G TGZ \
&& popd