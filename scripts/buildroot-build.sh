#!/bin/sh

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

# from https://medium.com/redbubble/running-a-docker-container-as-a-non-root-user-7d2e00f8ee15
# and https://jtreminio.com/blog/running-docker-containers-as-current-host-user/

#if [ "$#" -eq 1 ]; then
#    echo "Usage: $0 [release|devel]"
#    exit 1
#fi

BUILD_DIR=build-buildroot-release
DOCKER_IMAGE=dilshodm/sdk-builder-buildroot-arm64:1

CMD="cmake -DCMAKE_TOOLCHAIN_FILE=/toolchain/toolchain.cmake -DSCORBIT_SDK_INSTALL_PREFIX=/usr/lib -G Ninja -B '$BUILD_DIR' . && cmake --build '$BUILD_DIR' --config Release && cd '$BUILD_DIR' && cpack -G STGZ"
echo $CMD

docker container run --rm -it -v $(pwd):/src --workdir /src --user $(id -u):$(id -g) --platform linux/amd64 $DOCKER_IMAGE bash -c "$CMD"
