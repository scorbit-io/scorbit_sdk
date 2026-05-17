# Introduction

The Scorbit SDK enables seamless integration of third-party systems, machines, or platforms with the Scorbit ecosystem. It provides game manufacturers with the tools necessary to connect and interact with Scorbit services.

## Features

* Multi-language Support: Includes wrappers for multiple programming languages. Currently C, C++ and Python are supported. More languages will be added if necessary.
* Comprehensive Examples: Provides example implementations to help developers get started quickly.

# Getting Started

## Install Pre-built Package

It's recommended to use pre-built package. For obfuscation purposes, in pre-built packages, a shared secret is used to decrypt providers' private keys which is obtained using encrypt_tool. This allows developers to use private keys without exposing them in the code.

Pre-built packages are available for various platforms and processor architectures upon providers requests. Additional platforms will be supported on request.

From [releases](https://github.com/scorbit-io/scorbit_sdk/releases) page the necessary package can be downloaded.

Currently, they are available as `DEB` and `TGZ` packages for C/C++ and as wheel files `WHL` for Python.

Linux packages install under **`/opt/scorbit`** (not under `/usr/local`). Maintainer script sources live under **`assets/deb/`** (like scorbitd). The runtime Debian package installs `postinst`/`postrm`/`prerm` scripts that add `/etc/ld.so.conf.d/scorbit-sdk.conf` (listing `/opt/scorbit/lib`), run `ldconfig`, set **`/opt/scorbit/lib`** to mode **`777`** (world-writable, no sticky bit) so a non-root process can rename root-owned `.so` files for self-update, and run **`bin/add-rpi-rp2-fstab.sh add`** to add the same **RPI-RP2** `/etc/fstab` block used by scorbitd (mount label `RPI-RP2` at `/mnt/RPI-RP2`). The fstab helper is shipped from **`assets/scripts/`** to **`/opt/scorbit/bin/`** and is **not** placed on `PATH`; it is only invoked from package maintainer scripts. On remove/upgrade, `prerm` runs **`add ... remove`**. Tighter production setups can adjust permissions or fstab after install.

### Install C/C++ SDK

#### Choose the right pre-built package based on your system OS and architecture

The pre-built packages are available for different platforms and architectures. Choose the appropriate package based on your system configuration: [`<arch>`](#processor-architecture-arch) and [`<abi_tag>`](#abi-tag-abi_tag) (e.g. `scorbit_sdk-1.1.0-amd64_u20.tar.gz`):

**Debian/Ubuntu (.deb):** there are two packages per build:

* **`scorbit_sdk`**: shared library, linker configuration, and post-install hooks (runtime only).
* **`scorbit_sdk-dev`**: headers, CMake package files, and the shared-library symlink used when linking (`Depends` on the matching `scorbit_sdk`).

Example file names (`unknown` is not used in production builds):

```
scorbit_sdk-<version>-<arch>_<abi_tag>.deb
scorbit_sdk-dev-<version>-<arch>_<abi_tag>.deb
```

**Tarball (.tar.gz):** two archives per build, with the same split as the `.deb` packages:

* **`scorbit_sdk`**: shared library and `bin/add-rpi-rp2-fstab.sh` (runtime).
* **`scorbit_sdk-dev`**: headers and CMake package files (development).

Example file names:

```
scorbit_sdk-<version>-<arch>_<abi_tag>.tar.gz
scorbit_sdk-dev-<version>-<arch>_<abi_tag>.tar.gz
```

Extract both under the same prefix (e.g. `/opt/scorbit`) to reproduce the full install tree.

#### Processor Architecture (`<arch>`)

The SDK is built for multiple architectures. We support the following architectures:

* `amd64`: Intel/AMD x86_64 architecture
* `arm64`: ARM 64-bit architecture (AARCH64)
* `armhf`: ARM 32-bit architecture with hardware floating point (ARMv7, armhf) 

#### ABI Tag (`<abi_tag>`)

The ABI tag is used to specify the Application Binary Interface (ABI) compatibility of the package. It indicates the compatibility of the package with different versions of the Scorbit SDK. The ABI tag is included in the package name and can be used to ensure that the correct version of the SDK is installed.

The following are supported ABI tags:

* `u12`: Ubuntu 12.04 and later, Linux systems with glibc 2.15 or later
* `u12dev`: Same as `u12`, but built with OpenSSL built with "no-asm" option, which is useful for debugging purposes
* `u20`: Ubuntu 20.04 and later, Linux systems with glibc 2.31 and libstdc++.6.0.28 (GLIBCXX_3.4.28) or later

More compilers and OS will be added on request.

### Install Python SDK

Minimum supported Python version is 3.8 (Python 2.7 wheels are also published as **`scorbit`** with a `py2-none-any` tag).

The PyPI package is a **pure-Python** wrapper. Install it with pip, then install the **native** SDK for the same version from [GitHub releases](https://github.com/scorbit-io/scorbit_sdk/releases):

```bash
pip install scorbit==1.99.66
# Install native runtime from https://github.com/scorbit-io/scorbit_sdk/releases/tag/1.99.66
# e.g. scorbit_sdk-1.99.66-arm64_u18.deb or .tar.gz (see arch / ABI tags above)
```

If the shared library is not found, `import scorbit` reports the matching release URL and likely download filenames for your system.

## Install from Source

<Work in progress>

## Documentation

Detailed documentation is available [here](https://support.scorbit.io/sdk/sdk-001-introduction/). It covers the SDK's API references and integration guidelines.

## Examples

Example implementations can be found in the `examples` directory. These examples demonstrate how to integrate and utilize the SDK in various scenarios.

There are examples for C, C++, and Python languages. Each example is self-contained and can run in staging environment. However, it's strongly recommended to use your own provider's private key and machine ID obtained from Scorbit support.

## Support

Please, create an issue in the [GitHub repository](https://github.com/scorbit-io/scorbit_sdk/issues) if you encounter any problems or have questions/requests regarding the SDK.

## Contact

Scorbit is a platform that includes software, services, multiple applications, and hardware. We're not just one app, we're many apps, and we don't always require hardware to function. Get to know your particular use case and feel free to ask questions at any time by emailing support@scorbit.io.

## License

This project is licensed under the MIT license.

(c) 2025 Spinner Systems, Inc. (DBA Scorbit), All Rights Reserved