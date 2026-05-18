# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Helpers for native SDK install guidance when the shared library is missing."""

from __future__ import absolute_import

import ctypes
import os
import platform

try:
    from typing import List, Optional, Tuple
except ImportError:
    List = list  # type: ignore
    Optional = object  # type: ignore
    Tuple = tuple  # type: ignore

_RELEASES_REPO = "https://github.com/scorbit-io/scorbit_sdk"
_DOCS_INSTALL = "{}/blob/main/README.md#install-pre-built-package".format(_RELEASES_REPO)

# Production ABI per arch (scripts/linux-build.sh). Matches SCORBIT_SDK_PLATFORM_ID.
_LINUX_ABI_BY_ARCH = {
    "armhf": "u12",
    "arm64": "u18",
    "amd64": "u18",
}


def release_page_url(version):
    # type: (str) -> str
    """URL of the GitHub release for *version* (tags have no ``v`` prefix)."""
    return "{}/releases/tag/{}".format(_RELEASES_REPO, version)


def docs_install_url():
    # type: () -> str
    return _DOCS_INSTALL


def detect_arch():
    # type: () -> Optional[str]
    """Map :func:`platform.machine` to release artifact arch (``amd64``, ``arm64``, ``armhf``)."""
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64", "x64"):
        return "amd64"
    if machine in ("aarch64", "arm64"):
        return "arm64"
    if machine in ("armv7l", "armv6l", "armhf"):
        return "armhf"
    return None


def _glibc_version():
    # type: () -> Optional[Tuple[int, int]]
    if platform.system() != "Linux":
        return None
    try:
        libc = ctypes.CDLL("libc.so.6")
        fn = libc.gnu_get_libc_version
        fn.restype = ctypes.c_char_p
        raw = fn().decode("ascii", "replace")
        parts = raw.split(".")
        return (int(parts[0]), int(parts[1]) if len(parts) > 1 else 0)
    except (OSError, AttributeError, ValueError, TypeError):
        return None


def detect_platform_id():
    # type: () -> Optional[str]
    """Detect release platform id (``<arch>_<abi>``), same as native package names.

    Examples: ``arm64_u18``, ``amd64_u18``, ``armhf_u12``, ``arm64_macos``.
    """
    system = platform.system()
    arch = detect_arch()

    if system == "Darwin" and arch is not None:
        return "{}_macos".format(arch)

    if system == "Linux" and arch is not None:
        abi = _LINUX_ABI_BY_ARCH.get(arch)
        if abi is not None:
            return "{}_{}".format(arch, abi)

    return None


def describe_platform():
    # type: () -> str
    """Human-readable summary of the detected OS and CPU."""
    parts = [platform.system(), platform.machine()]
    glibc = _glibc_version()
    if glibc is not None:
        parts.append("glibc {}.{}".format(glibc[0], glibc[1]))
    return ", ".join(parts)


def is_deb_system():
    # type: () -> bool
    """True on Debian, Ubuntu, Raspberry Pi OS, and other dpkg-based Linux."""
    if platform.system() != "Linux":
        return False
    if os.path.isfile("/etc/debian_version"):
        return True
    if os.path.isfile("/usr/bin/dpkg") or os.path.isfile("/bin/dpkg"):
        return True
    try:
        with open("/etc/os-release", "r") as f:
            content = f.read()
    except (IOError, OSError):
        return False
    deb_family = ("debian", "ubuntu", "raspbian", "linuxmint", "pop", "elementary")
    for line in content.splitlines():
        line = line.strip()
        if line.startswith("ID="):
            os_id = line.split("=", 1)[1].strip().strip('"').lower()
            if os_id in deb_family:
                return True
        elif line.startswith("ID_LIKE="):
            id_like = line.split("=", 1)[1].strip().strip('"').lower()
            if "debian" in id_like.split():
                return True
    return False


def suggest_abi_tags():
    # type: () -> List[str]
    """Return the production ABI tag for this system (at most one)."""
    platform_id = detect_platform_id()
    if platform_id is None:
        return []
    if "_" not in platform_id:
        return []
    return [platform_id.rsplit("_", 1)[-1]]


def suggest_native_artifacts(version):
    # type: (str) -> List[str]
    """Return likely native package filenames for this machine and *version*."""
    system = platform.system()
    platform_id = detect_platform_id()
    lines = []  # type: List[str]

    if system == "Linux" and platform_id is not None:
        base = "scorbit_sdk-{}-{}".format(version, platform_id)
        if is_deb_system():
            lines.append("{}.deb".format(base))
        else:
            lines.append("{}.tar.gz".format(base))
        return lines

    if system == "Darwin" and platform_id is not None:
        lines.append("scorbit_sdk-{}-{}.tar.gz".format(version, platform_id))
        return lines

    if system == "Windows":
        lines.append(
            "Check the release page for Windows packages containing scorbit_sdk.dll"
        )
        return lines

    lines.append(
        "See the release assets list for packages matching your OS and CPU architecture"
    )
    return lines


def format_missing_library_message(version, lib_filename):
    # type: (str, str) -> str
    """Build the ImportError text when the native shared library was not found."""
    release_url = release_page_url(version)
    artifacts = suggest_native_artifacts(version)
    artifact_block = "\n".join("  {}".format(name) for name in artifacts)

    platform_id = detect_platform_id()
    if platform_id is not None:
        platform_line = "Detected platform: {} ({})\n\n".format(
            platform_id, describe_platform()
        )
    else:
        platform_line = "Detected system: {}\n\n".format(describe_platform())

    linux_note = ""
    if platform.system() == "Linux":
        linux_note = (
            "\n"
            "Then either:\n"
            "  - Install the runtime package under /opt/scorbit/lib (recommended), or\n"
            "  - Set SCORBIT_SDK_PATH to the directory containing {}\n".format(
                lib_filename
            )
        )
    else:
        linux_note = (
            "\n"
            "Then set SCORBIT_SDK_PATH to the directory containing {}.\n".format(
                lib_filename
            )
        )

    return (
        "Cannot find the Scorbit SDK shared library ({}).\n"
        "\n"
        "Python package scorbit {} requires the native SDK for this platform.\n"
        "\n"
        "{}" 
        "Install from:\n"
        "  {}\n"
        "\n"
        "Likely downloads for this system:\n"
        "{}\n"
        "{}"
        "\n"
        "See: {}".format(
            lib_filename,
            version,
            platform_line,
            release_url,
            artifact_block,
            linux_note,
            docs_install_url(),
        )
    )
