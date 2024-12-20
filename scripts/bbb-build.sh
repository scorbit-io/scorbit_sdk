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

CMD="cmake -G Ninja -B '$BUILD_DIR' -D BBB_BUILD=ON . && cmake --build '$BUILD_DIR' --config Release && cd '$BUILD_DIR' && cpack -G DEB"
echo $CMD

docker container run --rm -it -v $(pwd):/src --workdir /src --user $(id -u):$(id -g) --platform linux/arm/v7 $DOCKER_IMAGE bash -c "$CMD"
