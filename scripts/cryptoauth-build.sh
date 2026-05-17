#!/bin/bash

# Scorbit SDK - standalone cryptoauthlib shared library
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

source "$SCRIPT_DIR/_common.sh"

abi_for_arch() {
    case "$1" in
        armhf) echo u12 ;;
        arm64 | amd64) echo u18 ;;
        *)
            echo "Unsupported arch: $1 (use armhf, arm64, or amd64)" >&2
            return 1
            ;;
    esac
}

run_native() {
    bash ./scripts/cryptoauth-build-core.sh build/cryptoauth_shared
}

run_docker() {
    local arch=$1
    local abi
    abi=$(abi_for_arch "$arch")
    local build_dir="build/cryptoauth_${arch}_${abi}"
    local rel
    rel="$(cat "$REPO_ROOT/DOCKER_RELEASE")"
    local docker_image="dilshodm/gcc-builder:${rel}"

    ARCH=$arch
    echo "!!! Building cryptoauth for arch: $arch, ABI: $abi !!!"
    docker_build "bash ./scripts/cryptoauth-build-core.sh '$build_dir'" "$docker_image"
}

ARCH=${1:-}

if [[ -z "$ARCH" ]]; then
    run_native
else
    run_docker "$ARCH"
fi
