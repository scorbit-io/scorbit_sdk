#!/bin/sh

# Release build
docker build --platform linux/arm/v7 -t dilshodm/ubuntu-builder-arm:12.04 .

# Development build
docker build --platform linux/arm/v7 \
    --build-arg OPENSSL_CONFIG_ARGS="no-asm" \
    -t dilshodm/ubuntu-builder-arm:12.04-devel .
