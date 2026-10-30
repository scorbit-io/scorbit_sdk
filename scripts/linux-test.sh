#!/bin/bash

# Scorbit SDK
#
# (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# Build the test suite for <arch> in the release builder image, and run it when <arch> is this
# machine's own architecture. The image picks its toolchain and sysroot from ARCH, so the tests
# are compiled exactly as the release packages are.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$SCRIPT_DIR/.."

REL="$(tr -d '[:space:]' < "$SCRIPT_DIR/../DOCKER_RELEASE")"

source "$SCRIPT_DIR/_common.sh"

ARCH=${1:-}
case "$ARCH" in
    armhf | arm64 | amd64) ;;
    *)
        echo "Usage: $0 <arch>"
        echo "Arch: armhf, arm64, amd64"
        exit 1
        ;;
esac

BUILD_DIR="build/tests_${ARCH}"

# Debug, as the suite is built for development: it compiles the SDK with -Werror, and at Release
# optimisation GCC reports a stringop-overflow false positive inside Boost's own sha1.hpp.
# ctest runs in the container: CTestTestfile records container paths. A cross build (armhf on any
# host, or amd64/arm64 on the other one) cannot execute here, so it is compiled and not run.
# --no-tests=error: a suite that registered nothing must not pass.
# Catch2 lists a binary's tests when ctest runs rather than right after linking, which a cross
# build could not do.
CMD="cmake -D CMAKE_TOOLCHAIN_FILE=\"\$CMAKE_TOOLCHAIN_FILE\" -D CMAKE_BUILD_TYPE=Debug -G Ninja \
        -D CMAKE_CATCH_DISCOVER_TESTS_DISCOVERY_MODE=PRE_TEST \
        -B '$BUILD_DIR' -S tests \
    && cmake --build '$BUILD_DIR' --parallel \"\$(nproc)\" \
    && if [ \"\$(dpkg --print-architecture)\" = '$ARCH' ]; then \
        ctest --test-dir '$BUILD_DIR' --no-tests=error --output-on-failure --timeout 300; \
    else \
        echo 'Cross-compiled for $ARCH: tests built, not run'; \
    fi"

docker_build "$CMD" "dilshodm/gcc-builder:${REL}"
