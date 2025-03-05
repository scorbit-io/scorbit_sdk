#!/bin/sh

REL=2

# Determine the directory of the script
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Release build
docker build --platform linux/amd64 -t dilshodm/ubuntu-builder-amd64:20.04_${REL} "$SCRIPT_DIR"
