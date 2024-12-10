# Created by Dilshod Mukhtarov <dilshodm@gmail.com>, 2024
# License: MIT
# © 2024 Spinner Systems, Inc. (DBA Scorbit), All Rights Reserved

# Dockerfile for building the Scorbit SDK for the aarch64 architecture using given toolchain
# for buildroot based system (used by Pedretti)
FROM ubuntu:24.04

# Install dependencies
RUN \
    sed -i -e 's/archive.ubuntu.com\|security.ubuntu.com/old-releases.ubuntu.com/g' /etc/apt/sources.list \
    && apt-get update && apt-get install -y \
        cmake ninja-build git wget

ARG TOOLCHAIN_ARCHIVE

# Copy the toolchain archive into the image
COPY ${TOOLCHAIN_ARCHIVE} /tmp/

RUN mkdir /toolchain && cd /toolchain/ \
    && tar --strip-component 1 -xvf /tmp/${TOOLCHAIN_ARCHIVE} \
    && rm /tmp/${TOOLCHAIN_ARCHIVE}

WORKDIR /src

COPY toolchain.cmake /toolchain/toolchain.cmake

# Set the LD_LIBRARY_PATH to include /host/lib
ENV LD_LIBRARY_PATH=/toolchain/lib

CMD ["bash"]
