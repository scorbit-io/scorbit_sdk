#!/bin/bash

# Scorbit SDK - cross-build OpenSSH with PKCS#11 (armhf / arm64).
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
        arm64) echo u18 ;;
        *)
            echo "Unsupported arch: $1 (use armhf or arm64)" >&2
            return 1
            ;;
    esac
}

run_docker() {
    local arch=$1
    local abi
    abi=$(abi_for_arch "$arch")
    local build_dir="build/openssh_${arch}_${abi}"
    local rel
    rel="$(cat "$REPO_ROOT/DOCKER_RELEASE")"
    local docker_image="dilshodm/gcc-builder:${rel}"

    ARCH=$arch
    BUILD_DIR=$build_dir
    echo "!!! Building OpenSSH for arch: $arch, ABI: $abi !!!"
    docker_build "bash ./scripts/openssh-build-core.sh '$build_dir'" "$docker_image"
}

ARCH=${1:-}

if [[ -z "$ARCH" ]]; then
    echo "Usage: $0 <armhf|arm64>" >&2
    echo "  make openssh armhf" >&2
    echo "  make openssh arm64" >&2
    exit 1
fi

run_docker "$ARCH"
