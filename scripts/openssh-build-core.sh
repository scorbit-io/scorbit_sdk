#!/bin/bash

# Scorbit SDK - cross-build OpenSSH portable with PKCS#11 (runs inside gcc-builder).
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_ROOT="$1"
ARCH=${ARCH:-}

if [[ -z "$BUILD_ROOT" || -z "$ARCH" ]]; then
    echo "Usage: openssh-build-core.sh <BUILD_ROOT>  (requires env ARCH=armhf|arm64)" >&2
    exit 1
fi

if [[ "$ARCH" != "armhf" && "$ARCH" != "arm64" ]]; then
    echo "Unsupported ARCH=$ARCH (use armhf or arm64)" >&2
    exit 1
fi

SCORBIT_OPENSSH_PREFIX=/usr/local/scorbit/openssh

OPENSSH_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/OPENSSH_VERSION")"
OPENSSH_TARBALL="openssh-${OPENSSH_VERSION}.tar.gz"
OPENSSH_URL="https://ftp.openbsd.org/pub/OpenBSD/OpenSSH/portable/${OPENSSH_TARBALL}"
CACHE_DIR="${CPM_SOURCE_CACHE:-$REPO_ROOT/build/_cache}/openssh"
SRC_PARENT="$BUILD_ROOT/src"
STAGING_DIR="$BUILD_ROOT/staging"
DIST_DIR="$BUILD_ROOT/dist"

case "$ARCH" in
    armhf)
        GNU_HOST=arm-linux-gnueabihf
        TC_ROOT=/opt/armhf
        ;;
    arm64)
        GNU_HOST=aarch64-linux-gnu
        TC_ROOT=/opt/arm64
        ;;
esac

SYSROOT="${TC_ROOT}/sysroot"
TOOLCHAIN_BIN="${TC_ROOT}/toolchain/bin"
export PATH="${TOOLCHAIN_BIN}:${PATH}"

CC_BIN="${TOOLCHAIN_BIN}/${GNU_HOST}-gcc"
CXX_BIN="${TOOLCHAIN_BIN}/${GNU_HOST}-g++"
AR="${TOOLCHAIN_BIN}/${GNU_HOST}-ar"
RANLIB="${TOOLCHAIN_BIN}/${GNU_HOST}-ranlib"
STRIP="${TOOLCHAIN_BIN}/${GNU_HOST}-strip"
CC="$CC_BIN"
CXX="$CXX_BIN"
SYSROOT_CFLAGS="--sysroot=${SYSROOT}"
export CFLAGS="${SYSROOT_CFLAGS} ${CFLAGS:-}"
export CXXFLAGS="${SYSROOT_CFLAGS} ${CXXFLAGS:-}"
export PKG_CONFIG_PATH=""
export PKG_CONFIG_LIBDIR="${SYSROOT}/usr/local/lib/pkgconfig:${SYSROOT}/usr/lib/pkgconfig"

if [[ ! -x "$CC_BIN" ]]; then
    echo "Cross compiler not found: $CC_BIN" >&2
    exit 1
fi

if [[ ! -d "${SYSROOT}/usr/local/include/openssl" ]]; then
    echo "OpenSSL headers not found under ${SYSROOT}/usr/local (build sysroot OpenSSL first)." >&2
    exit 1
fi

mkdir -p "$CACHE_DIR" "$BUILD_ROOT" "$DIST_DIR"
BUILD_ROOT="$(cd "$BUILD_ROOT" && pwd)"
DIST_DIR="$BUILD_ROOT/dist"
SRC_PARENT="$BUILD_ROOT/src"
STAGING_DIR="$BUILD_ROOT/staging"
ZLIB_PREFIX="$BUILD_ROOT/zlib-install"
if [[ ! -f "$CACHE_DIR/$OPENSSH_TARBALL" ]]; then
    echo "Downloading $OPENSSH_URL"
    curl -fsSL "$OPENSSH_URL" -o "$CACHE_DIR/$OPENSSH_TARBALL"
fi

rm -rf "$SRC_PARENT"
mkdir -p "$SRC_PARENT"
read -r _tar_first_line < <(tar -tzf "$CACHE_DIR/$OPENSSH_TARBALL")
TOP_DIR="${_tar_first_line%%/*}"
tar -xzf "$CACHE_DIR/$OPENSSH_TARBALL" -C "$SRC_PARENT"
SRC_DIR="$SRC_PARENT/${TOP_DIR}"
[[ -d "$SRC_DIR" ]] || { echo "Expected source dir under $SRC_PARENT" >&2; exit 1; }

rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"

ZLIB_VERSION=1.3.1
ZLIB_TARBALL="zlib-${ZLIB_VERSION}.tar.gz"
ZLIB_URL="https://zlib.net/fossils/${ZLIB_TARBALL}"

if [[ ! -f "${ZLIB_PREFIX}/include/zlib.h" ]]; then
    echo "Building zlib ${ZLIB_VERSION} for ${ARCH}"
    if [[ ! -f "$CACHE_DIR/$ZLIB_TARBALL" ]]; then
        curl -fsSL "$ZLIB_URL" -o "$CACHE_DIR/$ZLIB_TARBALL"
    fi
    ZLIB_SRC="$BUILD_ROOT/zlib-src"
    rm -rf "$ZLIB_SRC"
    mkdir -p "$ZLIB_SRC"
    tar -xzf "$CACHE_DIR/$ZLIB_TARBALL" -C "$ZLIB_SRC" --strip-components=1
    (
        cd "$ZLIB_SRC"
        # -fPIC: OpenSSH links with -pie; non-PIC static zlib triggers ARM relocation errors.
        CFLAGS="${CFLAGS:-} -fPIC" \
            CC="$CC" AR="$AR" RANLIB="$RANLIB" \
            ./configure --prefix="$ZLIB_PREFIX" --static
        make -j"$(nproc)"
        make install
    )
fi

if [[ ! -f "${ZLIB_PREFIX}/include/zlib.h" || ! -f "${ZLIB_PREFIX}/lib/libz.a" ]]; then
    echo "zlib install incomplete under ${ZLIB_PREFIX}" >&2
    exit 1
fi

# PKCS#11Provider is enabled when OpenSSL is found (see INSTALL in openssh-portable).
export CPPFLAGS="-I${SYSROOT}/usr/local/include -I${ZLIB_PREFIX}/include ${CPPFLAGS:-}"
export LDFLAGS="--sysroot=${SYSROOT} -L${ZLIB_PREFIX}/lib -L${SYSROOT}/usr/local/lib -Wl,-rpath-link,${SYSROOT}/usr/local/lib -Wl,-rpath-link,${SYSROOT}/usr/lib/${GNU_HOST} -Wl,-rpath-link,${SYSROOT}/lib/${GNU_HOST} ${LDFLAGS:-}"
# libcrypto.a needs pthread when configure link-tests OpenSSL.
export LIBS="-lpthread ${LIBS:-}"

cd "$SRC_DIR"

./configure \
    --host="$GNU_HOST" \
    --prefix="$SCORBIT_OPENSSH_PREFIX" \
    --sysconfdir="${SCORBIT_OPENSSH_PREFIX}/etc/ssh" \
    --with-privsep-path=/var/empty \
    --with-privsep-user=sshd \
    --with-ssl-dir="${SYSROOT}/usr/local" \
    --with-zlib="${ZLIB_PREFIX}" \
    --without-zlib-version-check \
    --without-pam \
    --without-selinux \
    --disable-utmp \
    --disable-wtmp \
    --disable-lastlog \
    --without-strip

grep -qE '^#define ENABLE_PKCS11\b' config.h || {
    echo "ENABLE_PKCS11 not set in config.h — OpenSSH was built without PKCS#11 support." >&2
    exit 1
}

make -j"$(nproc)"
make install-nokeys DESTDIR="$STAGING_DIR"

PKG_NAME="openssh-${OPENSSH_VERSION}-${ARCH}"
PKG_DIR="$DIST_DIR/$PKG_NAME"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"
cp -a "$STAGING_DIR/." "$PKG_DIR/"

(
    cd "$DIST_DIR"
    tar -czf "${PKG_NAME}.tar.gz" "$PKG_NAME"
)

echo "OpenSSH ${OPENSSH_VERSION} for ${ARCH}:"
echo "  prefix:  ${SCORBIT_OPENSSH_PREFIX}"
echo "  staging: ${STAGING_DIR}"
echo "  package: ${DIST_DIR}/${PKG_NAME}.tar.gz"
find "$PKG_DIR" -maxdepth 3 \( -name ssh -o -name sshd -o -name scp \) 2>/dev/null
