# Makefile for Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

.PHONY: all clean armhf arm64 amd64 macos python

ARCHS := armhf arm64 amd64

all: 
	$(ARCHS)
	python

armhf arm64 amd64:
	./scripts/l-build.sh $@

macos:
	./scripts/macos-build.sh

python:
	./scripts/python-build.sh

clean:
	rm -rf build/armhf_u12 build/arm64_u20 build/amd64_u20
