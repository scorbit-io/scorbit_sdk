# Makefile for Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

.PHONY: help all clean armhf arm64 amd64 macos python python27

.DEFAULT_GOAL := help

help:
	@echo "Scorbit SDK build targets:"
	@echo ""
	@echo "  make all        - Build all Linux architectures + Python wheels"
	@echo "  make armhf      - Build for ARM hard-float (Linux)"
	@echo "  make arm64      - Build for ARM 64-bit (Linux)"
	@echo "  make amd64      - Build for x86_64 (Linux)"
	@echo "  make macos      - Build for macOS"
	@echo "  make python     - Build Python 3.6+ wheel"
	@echo "  make python27   - Build Python 2.7 wheel"
	@echo "  make clean      - Remove build artifacts"

all: armhf arm64 amd64 python python27

armhf arm64 amd64:
	./scripts/l-build.sh $@

macos:
	./scripts/macos-build.sh

python:
	./scripts/python-build.sh

python27:
	./scripts/python27-build.sh

clean:
	rm -rf build/armhf_u12 build/arm64_u20 build/amd64_u20
