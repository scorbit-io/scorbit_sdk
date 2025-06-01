# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

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
        pkg-config \
        ninja-build \
        perl \
        linux-headers-5.4.0-204 \
        libssl-dev

ARG ZLIB=zlib-1.3.1
RUN wget --no-check-certificate https://zlib.net/$ZLIB.tar.gz \
    && tar xf $ZLIB.tar.gz && rm $ZLIB.tar.gz && cd $ZLIB \
    && CFLAGS="-fPIC" ./configure --static \
    && make -j$(nproc) \
    && make install \
    && cd .. && rm -rf $ZLIB

ARG OPENSSL=openssl-3.5.0
ARG OPENSSL_CONFIG_ARGS=""
RUN wget https://github.com/openssl/openssl/releases/download/$OPENSSL/$OPENSSL.tar.gz \
    && tar xf $OPENSSL.tar.gz && rm $OPENSSL.tar.gz && cd $OPENSSL \
    && ./config --openssldir=/usr/lib/ssl --prefix=/usr/local --libdir=/usr/local/lib \
                no-apps no-shared no-pinshared zlib \
                ${OPENSSL_CONFIG_ARGS} \
    && make -j $(nproc) && make install_sw \
    && cd .. && rm -rf $OPENSSL

# Install CMake
ARG CMAKE_VERSION=4.0.1
RUN ARCH=$(uname -m) \
    && wget https://github.com/dilshodm/cmake-arm32v7/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-Linux-$ARCH.sh \
    && sh cmake-$CMAKE_VERSION-Linux-$ARCH.sh --skip-license --prefix=/usr \
    && rm cmake-$CMAKE_VERSION-Linux-$ARCH.sh

# libspl needed for curl
ARG LIBPSL_VER=0.21.5
ARG LIBPSL=libpsl-$LIBPSL_VER
RUN wget https://github.com/rockdaboot/libpsl/releases/download/$LIBPSL_VER/$LIBPSL.tar.gz \
    && tar xf $LIBPSL.tar.gz && rm $LIBPSL.tar.gz && cd $LIBPSL \
    && CFLAGS="-fPIC" CXXFLAGS="-fPIC" \
       ./configure --enable-static --disable-shared \
    && make -j$(nproc) \
    && make install \
    && cd .. && rm -rf $LIBPSL

ARG CURL=curl-8.14.0
ARG CURL_VER=8_14_0
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
    && cd .. && rm -rf $CURL

ARG LIBARCHIVE_VER=3.8.0
ARG LIBARCHIVE=libarchive-$LIBARCHIVE_VER
RUN wget https://github.com/libarchive/libarchive/releases/download/v$LIBARCHIVE_VER/$LIBARCHIVE.tar.xz \
    && tar xf $LIBARCHIVE.tar.xz && rm $LIBARCHIVE.tar.xz && cd $LIBARCHIVE \
    && CFLAGS="-fPIC" CXXFLAGS="-fPIC" \
       LDFLAGS="-L/usr/local/lib -lcrypto -lssl -ldl -lpthread" \
       ./configure --with-openssl --disable-shared \
    && make -j$(nproc) \
    && make install \
    && cd .. && rm -rf $LIBARCHIVE

ARG BOOST=boost-1.88.0
RUN wget https://github.com/boostorg/boost/releases/download/$BOOST/$BOOST-b2-nodocs.tar.xz \
    && tar xf $BOOST-b2-nodocs.tar.xz && rm $BOOST-b2-nodocs.tar.xz && cd $BOOST \
    && ./bootstrap.sh \
    && ./b2 -j$(nproc) threading=multi link=static cflags="-fPIC" cxxflags="-fPIC" --without-python install \
    && cd .. && rm -rf $BOOST

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
