# Created by Dilshod Mukhtarov <dilshodm@gmail.com>, 2024
# License: MIT
# © 2024 Spinner Systems, Inc. (DBA Scorbit), All Rights Reserved

# Dockerfile for building the Scorbit SDK for the amd64 architecture running Ubuntu 20.04

FROM ubuntu:20.04

ARG TARGETPLATFORM
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
        libssl-dev

ARG ZLIB=zlib-1.3.1
RUN wget --no-check-certificate https://zlib.net/$ZLIB.tar.gz \
    && tar xvf $ZLIB.tar.gz && rm $ZLIB.tar.gz && cd $ZLIB \
    && CFLAGS="-fPIC" ./configure --static \
    && make -j$(nproc) \
    && make install \
    && cd .. && rm -rf $ZLIB

ARG OPENSSL=openssl-3.4.1
ARG OPENSSL_CONFIG_ARGS=""
RUN wget https://github.com/openssl/openssl/releases/download/$OPENSSL/$OPENSSL.tar.gz \
    && tar xvf $OPENSSL.tar.gz && rm $OPENSSL.tar.gz && cd $OPENSSL \
    && ./config --openssldir=/usr/lib/ssl --prefix=/usr/local --libdir=/usr/local/lib \
                no-apps no-shared no-pinshared zlib \
                ${OPENSSL_CONFIG_ARGS} \
    && make -j $(nproc) && make install_sw \
    && cd .. && rm -rf $OPENSSL

# Install CMake
ARG CMAKE_VERSION=3.31.5
RUN ARCH=$(uname -m) \
    && wget https://github.com/dilshodm/cmake-arm32v7/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-Linux-$ARCH.sh \
    && sh cmake-$CMAKE_VERSION-Linux-$ARCH.sh --skip-license --prefix=/usr \
    && rm cmake-$CMAKE_VERSION-Linux-$ARCH.sh

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

# Install Python versions and create virtual environments
RUN add-apt-repository -y ppa:deadsnakes/ppa \
    && apt update \
    &&  for ver in 3.8 3.9 3.10 3.11 3.12 3.13; do \
            apt install -y python$ver python$ver-dev python$ver-venv && \
            python$ver -m venv /opt/venv$ver && \
            . /opt/venv$ver/bin/activate && \
            python$ver -m pip install --upgrade pip build && \
            deactivate \
        ; done
