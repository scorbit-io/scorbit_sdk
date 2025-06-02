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

### Install C/C++ SDK

#### Choose the right pre-built package based on your system OS and architecture

The pre-built packages are available for different platforms and architectures. Choose the appropriate package based on your system configuration: [`<arch>`](#processor-architecture-arch) and [`<abi_tag>`](#abi-tag-abi_tag) (e.g. `scorbit_sdk-1.1.0-amd64_u20.tar.gz`):

```
scorbit-sdk-<version>-<arch>_<abi_tag>.deb
scorbit-sdk-<version>-<arch>_<abi_tag>.tgz
```

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

Minimum supported Python version is 3.8.

To install the Python SDK, you can use pip to install the pre-built wheel package (e.g., for Python 3.10 on amd64 linux it will be `scorbit-1.0.3-cp310-cp310-linux_x86_64.whl`).

```bash
pip install scorbit-1.0.3-cp310-cp310-linux_x86_64.whl
```

## Install from Source

<Work in progress>

## Documentation

Detailed documentation is available [here](https://support.scorbit.io/sdk/sdk-001-introduction/). It covers the SDK’s API references and integration guidelines.

## Examples

Example implementations can be found in the `examples` directory. These examples demonstrate how to integrate and utilize the SDK in various scenarios.

There are examples for C, C++, and Python languages. Each example is self-contained and can run in staging environment. However, it's strongly recommended to use your own provider's private key and machine ID obtained from Scorbit support.

## Support

Please, create an issue in the [GitHub repository](https://github.com/scorbit-io/scorbit_sdk/issues) if you encounter any problems or have questions/requests regarding the SDK.

## Contact

Scorbit is a platform that includes software, services, multiple applications, and hardware. We're not just one app, we're many apps, and we don't always require hardware to function. Get to know your particular use case and feel free to ask questions at any time by emailing support@scorbit.io.

## License

This project is licensed under the MIT license.

© 2025 Spinner Systems, Inc. (DBA Scorbit), All Rights Reserved