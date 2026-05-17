#!/bin/bash

# Scorbit SDK - standalone cryptoauthlib (runs inside gcc-builder when cross-compiling)
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License

set -e

BUILD_DIR=$1

if [[ -z "$BUILD_DIR" ]]; then
    echo "Usage: cryptoauth-build-core.sh <BUILD_DIR>"
    exit 1
fi

CMAKE_TOOLCHAIN_ARGS=()
if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    CMAKE_TOOLCHAIN_ARGS=(-D "CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
fi

cmake -G Ninja -B "$BUILD_DIR" -S libs/cryptoauth_shared \
    "${CMAKE_TOOLCHAIN_ARGS[@]}" \
    -D CMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR"
find "$BUILD_DIR" -name 'libcryptoauth.so*' 2>/dev/null | head -5
