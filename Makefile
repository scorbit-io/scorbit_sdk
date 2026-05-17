# Makefile for Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

.PHONY: help all clean armhf arm64 amd64 macos python python27 release cryptoauth

RELEASE_VERSION := $(shell tr -d '[:space:]' < VERSION)

.DEFAULT_GOAL := help

# When both cryptoauth and an arch appear on the command line (e.g. make cryptoauth armhf),
# build only the shared cryptoauthlib for that arch in Docker; do not run the full SDK build.
CRYPTOAUTH_ARCH_GOALS := $(filter armhf arm64 amd64,$(MAKECMDGOALS))
ifneq (,$(filter cryptoauth,$(MAKECMDGOALS)))
ifneq (,$(CRYPTOAUTH_ARCH_GOALS))
CRYPTOAUTH_ARCH := $(firstword $(CRYPTOAUTH_ARCH_GOALS))
endif
endif
ifdef ARCH
CRYPTOAUTH_ARCH := $(ARCH)
endif

help:
	@echo "Scorbit SDK build targets:"
	@echo ""
	@echo "  make all        - Build all Linux architectures + Python wheels"
	@echo "  make armhf      - Build for ARM hard-float (Linux)"
	@echo "  make arm64      - Build for ARM 64-bit (Linux)"
	@echo "  make amd64      - Build for x86_64 (Linux)"
	@echo "  make macos      - Build for macOS"
	@echo "  make python     - Build Python 3.6+ wheel (Docker: dilshodm/python-builder, see DOCKER_RELEASE)"
	@echo "  make python27   - Build Python 2.7 wheel (same python-builder image)"
	@echo "  make release    - Create release branch and commits from VERSION"
	@echo "  make cryptoauth              - Build cryptoauthlib as standalone .so/.dylib on this host"
	@echo "  make cryptoauth armhf|arm64|amd64 - Cross-build standalone libcryptoauth.so (Docker/gcc-builder)"
	@echo "  make clean      - Remove build artifacts"
	@echo ""
	@echo "  SCORBIT_PYTHON_NO_DOCKER=1 make python   - Build Python wheel on host Python (no Docker)"

all: armhf arm64 amd64 python python27

armhf arm64 amd64:
ifneq (,$(filter cryptoauth,$(MAKECMDGOALS)))
	@:
else
	./scripts/linux-build.sh $@
endif

macos:
	./scripts/macos-build.sh

python:
	./scripts/python-build.sh

python27:
	./scripts/python27-build.sh

release:
	@if [ -z "$(RELEASE_VERSION)" ]; then \
		echo "VERSION is empty"; \
		exit 1; \
	fi
	@if git diff --quiet -- VERSION && git diff --cached --quiet -- VERSION; then \
		echo "Refusing release: VERSION is not modified"; \
		exit 1; \
	fi
	@if [ -n "$$(git status --porcelain --untracked-files=all -- . ':!VERSION')" ]; then \
		echo "Refusing release: staged or unstaged changes exist outside VERSION"; \
		git status --short -- . ':!VERSION'; \
		exit 1; \
	fi
	git switch -c "chore/release_$(RELEASE_VERSION)"
	@if ! git diff --cached --quiet -- VERSION; then \
		git restore --staged VERSION; \
	fi
	./scripts/update-cacert.sh
	git add assets/certs/cacert.pem
	@if ! git diff --cached --quiet -- assets/certs/cacert.pem; then \
		git commit -m "chore: update certs"; \
	else \
		echo "No certificate changes to commit"; \
	fi
	git add VERSION
	git commit -m "chore(release): bump version to $(RELEASE_VERSION)"

cryptoauth:
	bash ./scripts/cryptoauth-build.sh $(CRYPTOAUTH_ARCH)

clean:
	rm -rf build/armhf_* build/arm64_* build/amd64_*
	rm -rf build/cryptoauth_shared build/cryptoauth_*
