#!/bin/sh

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

# from https://medium.com/redbubble/running-a-docker-container-as-a-non-root-user-7d2e00f8ee15
# and https://jtreminio.com/blog/running-docker-containers-as-current-host-user/

BUILD_DIR=build-bbb
CMD="cmake -G Ninja -B '$BUILD_DIR' -D BBB_BUILD=ON . && cmake --build '$BUILD_DIR' --config Release && cd '$BUILD_DIR' && cpack -G DEB"
echo $CMD

docker container run --rm -it -v $(pwd):/src --workdir /src --user $(id -u):$(id -g) --platform linux/arm/v7 dilshodm/ubuntu-builder-arm:12.04 bash -c "$CMD"
