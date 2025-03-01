#!/bin/sh

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

# from https://medium.com/redbubble/running-a-docker-container-as-a-non-root-user-7d2e00f8ee15
# and https://jtreminio.com/blog/running-docker-containers-as-current-host-user/

if [ "$1" = "release" ]; then
    BUILD_DIR=build-bbb-release
    DOCKER_IMAGE=dilshodm/ubuntu-builder-arm:12.04
elif [ "$1" = "devel" ]; then
    BUILD_DIR=build-bbb-devel
    DOCKER_IMAGE=dilshodm/ubuntu-builder-arm:12.04-devel
else
    echo "Usage: $0 [release|devel]"
    exit 1
fi

CMD="
    cmake \
        -D BBB_BUILD=ON \
        -D SCORBIT_SDK_PRODUCTION=ON \
        -G Ninja \
        -B '$BUILD_DIR' \
        -S . \
    && cmake --build '$BUILD_DIR' --config Release \
    && cd '$BUILD_DIR' \
    && cpack -G DEB \
    \
    && cd .. \
    && cmake \
        -D BBB_BUILD=ON \
        -D SCORBIT_SDK_PRODUCTION=ON \
        -G Ninja \
        -B '$BUILD_DIR'/encrypt_tool \
        -S encrypt_tool \
    && cmake --build '$BUILD_DIR'/encrypt_tool --config Release \
    && cd '$BUILD_DIR'/encrypt_tool \
    && cpack -G TGZ
"
echo $CMD

docker container run --rm -it \
    -v $(pwd):/src \
    --workdir /src \
    --user $(id -u):$(id -g) \
    --platform linux/arm/v7 \
    -e SCORBIT_SDK_ENCRYPT_SECRET \
    $DOCKER_IMAGE bash -c "$CMD"
