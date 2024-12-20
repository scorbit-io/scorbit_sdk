# Created by Dilshod Mukhtarov <dilshodm@gmail.com>, 2024
# License: MIT
# © 2024 Spinner Systems, Inc. (DBA Scorbit), All Rights Reserved

# Dockerfile for building the Scorbit SDK for the amd64 architecture running Ubuntu 20.04

FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN \
    ln -fs /usr/share/zoneinfo/Etc/UTC /etc/localtime && echo "Etc/UTC" > /etc/timezone && \
    apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        wget \
        git \
        file \
        software-properties-common \
        \
        ninja-build \
        perl \
        linux-headers-5.4.0-204 \
        zlib1g-dev \
        libssl-dev

ARG OPENSSL=openssl-3.4.0
ARG OPENSSL_CONFIG_ARGS=""
RUN wget https://github.com/openssl/openssl/releases/download/$OPENSSL/$OPENSSL.tar.gz \
    && tar xvf $OPENSSL.tar.gz && rm $OPENSSL.tar.gz && cd $OPENSSL \
    && ./config --openssldir=/usr/lib/ssl --prefix=/usr/local --libdir=/usr/local/lib \
                no-apps shared zlib ${OPENSSL_CONFIG_ARGS} \
    && make -j $(nproc) && make install_sw \
    && cd .. && rm -rf $OPENSSL

# Install CMake
ARG CMAKE_VERSION=3.30.3
RUN wget https://github.com/dilshodm/cmake-arm32v7/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-Linux-x86_64.sh \
    && sh cmake-$CMAKE_VERSION-Linux-x86_64.sh --skip-license --prefix=/usr \
    && rm cmake-$CMAKE_VERSION-Linux-x86_64.sh

ARG CURL=curl-8.11.1
ARG CURL_VER=8_11_1
RUN wget https://github.com/curl/curl/releases/download/curl-$CURL_VER/$CURL.tar.xz \
    && tar xvf $CURL.tar.xz && rm $CURL.tar.xz && cd $CURL \
    && cmake -GNinja -Bbuild \
            -D CMAKE_BUILD_TYPE=Release \
            -D CMAKE_POSITION_INDEPENDENT_CODE=ON \
            -D BUILD_SHARED_LIBS=OFF \
            -D BUILD_CURL_EXE=OFF \
            -D BUILD_TESTING=OFF \
            -D SSL_ENABLED=ON \
            -D CURL_CA_PATH=auto \
            -D CURL_USE_OPENSSL=ON \
            . \
    && cmake --build build --parallel \
    && cmake --build build --target install \
    && cd ../.. && rm -rf $CURL
