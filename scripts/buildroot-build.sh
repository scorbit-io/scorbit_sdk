#!/bin/sh

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

# from https://medium.com/redbubble/running-a-docker-container-as-a-non-root-user-7d2e00f8ee15
# and https://jtreminio.com/blog/running-docker-containers-as-current-host-user/

BUILD_DIR=build-buildroot-release
DOCKER_IMAGE=dilshodm/sdk-builder-buildroot-arm64:1

CMD="
    cmake \
        -D CMAKE_TOOLCHAIN_FILE=/toolchain/toolchain.cmake \
        -D SCORBIT_SDK_INSTALL_PREFIX=/usr/lib \
        -D SCORBIT_SDK_PRODUCTION=ON \
        -G Ninja \
        -B '$BUILD_DIR' \
        -S . \
    && cmake --build '$BUILD_DIR' --config Release \
    && cd '$BUILD_DIR' \
    && cpack -G STGZ \
    && cpack -G TGZ
"

echo "$CMD"

docker container run --rm -it \
    -v $(pwd):/src \
    --workdir /src \
    --user $(id -u):$(id -g) \
    --platform linux/amd64 \
    -e SCORBIT_SDK_ENCRYPT_SECRET \
    $DOCKER_IMAGE bash -c "$CMD"
