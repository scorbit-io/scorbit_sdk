#!/bin/sh

# Dilshod Mukhtarov <dilshodm@gmail.com>, Oct 2024

# from https://medium.com/redbubble/running-a-docker-container-as-a-non-root-user-7d2e00f8ee15
# and https://jtreminio.com/blog/running-docker-containers-as-current-host-user/

#if [ "$#" -eq 1 ]; then
#    echo "Usage: $0 [release|devel]"
#    exit 1
#fi

REL=1

BUILD_DIR=build-ubuntu-amd64-20.04
DOCKER_IMAGE=dilshodm/ubuntu-builder-amd64:20.04_${REL}

CMD="cmake -G Ninja -B '$BUILD_DIR' -D UBUNTU_BUILD=ON . && cmake --build '$BUILD_DIR' --config Release && cd '$BUILD_DIR' && cpack -G DEB -G TGZ"
echo $CMD

docker container run --rm -it -v $(pwd):/src --workdir /src --user $(id -u):$(id -g) --platform linux/amd64 $DOCKER_IMAGE bash -c "$CMD"
