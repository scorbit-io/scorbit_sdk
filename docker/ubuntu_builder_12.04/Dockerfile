# Created by Dilshod Mukhtarov <dilshodm@gmail.com>, 2024
# License: MIT
# © 2024 Spinner Systems, Inc. (DBA Scorbit), All Rights Reserved

# Dockerfile for building the Scorbit SDK for the arm32v7 architecture on BeagleBone Black running Ubuntu 12.10

FROM dilshodm/ubuntu:12.04

# Install dependencies
RUN \
    sed -i -e 's/archive.ubuntu.com\|security.ubuntu.com/old-releases.ubuntu.com/g' /etc/apt/sources.list \
    && apt-get update && apt-get install -y \
        build-essential \
        wget \
        software-properties-common \
        python-software-properties \
        \
        ninja-build \
        perl \
        linux-headers-3.2.0-150 \
    && add-apt-repository ppa:ubuntu-toolchain-r/test \
    && add-apt-repository ppa:git-core/ppa \
    && apt-get update \
    && apt-get install -y g++-9 git \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 10 --slave /usr/bin/g++ g++ /usr/bin/g++-9

# Install newer perl modules, as in ubuntu 12.04 perl 5.14 is installed and it's too old
RUN apt-get install -y cpanminus \
    && cpanm List::Util Text::Template

#ARG ZLIB=zlib-1.3.1
#RUN wget https://zlib.net/$ZLIB.tar.gz \
#    && tar xvf $ZLIB.tar.gz && rm $ZLIB.tar.gz && cd $ZLIB \
#    && ./configure --static && make -j $(nproc) && make install \
#    && cd .. && rm -rf $ZLIB

RUN apt-get install -y zlib1g-dev

ARG OPENSSL=openssl-3.3.2
RUN wget https://github.com/openssl/openssl/releases/download/$OPENSSL/$OPENSSL.tar.gz \
    && tar xvf $OPENSSL.tar.gz && rm $OPENSSL.tar.gz && cd $OPENSSL \
    && ./config --openssldir=/usr/lib/ssl no-apps shared zlib && make -j $(nproc) && make install_sw \
    && cd .. && rm -rf $OPENSSL

# Install CMake
ARG CMAKE_VERSION=3.30.3
RUN wget https://github.com/dilshodm/cmake-arm32v7/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-Linux-armv7l.sh \
    && sh cmake-$CMAKE_VERSION-Linux-armv7l.sh --skip-license --prefix=/usr \
    && rm cmake-$CMAKE_VERSION-Linux-armv7l.sh

ARG CURL=curl-8.10.1
ARG CURL_VER=8_10_1
RUN wget https://github.com/curl/curl/releases/download/curl-$CURL_VER/$CURL.tar.xz \
    && tar xvf $CURL.tar.xz && rm $CURL.tar.xz && cd $CURL \
    && cmake -GNinja -Bbuild \
            -D CMAKE_BUILD_TYPE=Release \
            -D BUILD_SHARED_LIBS=ON \
            -D BUILD_CURL_EXE=OFF \
            -D BUILD_TESTING=OFF \
            -D SSL_ENABLED=ON \
            -D CURL_CA_PATH=auto \
            -D CURL_USE_OPENSSL=ON \
            . \
    && cmake --build build --parallel \
    && cmake --build build --target install \
    && cd ../.. && rm -rf $CURL
