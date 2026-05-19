#!/bin/bash

# Scorbit SDK
#
# (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License

set -euo pipefail

BUILD_DIR=${1:-}
ABI=${2:-}

if [[ -z "$BUILD_DIR" || -z "$ABI" ]]; then
    echo "Usage: build-core.sh <BUILD_DIR> <ABI>"
    exit 1
fi

if [[ -z "${SOURCE_DATE_EPOCH:-}" ]]; then
    SOURCE_DATE_EPOCH="$(git log -1 --format=%ct 2>/dev/null || true)"
    if [[ ! "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]]; then
        echo "SOURCE_DATE_EPOCH is not set and could not be derived from git." >&2
        exit 1
    fi
fi

export SOURCE_DATE_EPOCH
export TZ=UTC
export LC_ALL=C
export LANG=C
umask 022

touch_reproducible_tree() {
    local path=$1

    if [[ -e "$path" ]]; then
        find "$path" -exec touch -h -d "@${SOURCE_DATE_EPOCH}" {} +
    fi
}

rebuild_deb_root_owner_group() {
    local deb=$1
    local tmp_dir

    tmp_dir="$(mktemp -d)"
    dpkg-deb -R "$deb" "$tmp_dir/root"
    touch_reproducible_tree "$tmp_dir/root"
    dpkg-deb --build --root-owner-group -Zgzip -z9 --uniform-compression \
        "$tmp_dir/root" "$tmp_dir/rebuilt.deb" >/dev/null
    mv "$tmp_dir/rebuilt.deb" "$deb"
    rm -rf "$tmp_dir"
    touch -h -d "@${SOURCE_DATE_EPOCH}" "$deb"
}

normalize_debs() {
    shopt -s nullglob
    local debs=(*.deb)
    shopt -u nullglob

    for deb in "${debs[@]}"; do
        rebuild_deb_root_owner_group "$deb"
    done
}

run_cpack() {
    local generator=$1

    rm -rf _CPack_Packages
    cpack -G "$generator"

    if [[ "$generator" == "DEB" ]]; then
        normalize_debs
    fi
}

# Pass through when set in the environment (e.g. ENV in gcc-builder Docker image).
# Do not use \$ here - that sent the literal "$CMAKE_TOOLCHAIN_FILE" to CMake.
CMAKE_TOOLCHAIN_ARGS=()
if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
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
    && { if command -v dpkg-deb >/dev/null 2>&1; then run_cpack DEB; else echo "Skipping cpack DEB (dpkg-deb not found)"; fi; } \
    && run_cpack TGZ \
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
&& run_cpack TGZ \
&& popd
