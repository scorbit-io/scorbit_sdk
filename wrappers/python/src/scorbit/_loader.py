# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Shared library loader for the Scorbit SDK.

Locates and loads the native ``libscorbit_sdk`` shared library using:

1. The ``SCORBIT_SDK_PATH`` environment variable (directory containing the lib).
2. The default install location ``/opt/scorbit/lib`` (official Linux packages).
3. Standard system library paths (``LD_LIBRARY_PATH``, ``DYLD_LIBRARY_PATH``,
   system lib dirs via :func:`ctypes.util.find_library`).
"""

import ctypes
import ctypes.util
import os
import platform

from ._install_hints import format_missing_library_message
from ._version import __version__

_LIB_NAMES = {
    "Linux": "libscorbit_sdk.so",
    "Darwin": "libscorbit_sdk.dylib",
    "Windows": "scorbit_sdk.dll",
}


def _load_library():
    # type: () -> ctypes.CDLL
    system = platform.system()
    lib_filename = _LIB_NAMES.get(system)
    if lib_filename is None:
        raise ImportError(
            "Unsupported platform: {}. "
            "Scorbit SDK supports Linux, macOS, and Windows.".format(system)
        )

    # 1. Try SCORBIT_SDK_PATH environment variable
    sdk_path = os.environ.get("SCORBIT_SDK_PATH")
    if sdk_path:
        candidates = [os.path.join(sdk_path, lib_filename)]
        # Also check lib/ subdirectory
        candidates.append(os.path.join(sdk_path, "lib", lib_filename))
        for path in candidates:
            if os.path.isfile(path):
                try:
                    return ctypes.CDLL(path)
                except OSError:
                    pass

    # 2. Official package layout under /opt/scorbit
    for prefix in ("/opt/scorbit",):
        for path in (
            os.path.join(prefix, "lib", lib_filename),
            os.path.join(prefix, lib_filename),
        ):
            if os.path.isfile(path):
                try:
                    return ctypes.CDLL(path)
                except OSError:
                    pass

    # 3. Try standard system paths via ctypes.util.find_library
    found = ctypes.util.find_library("scorbit_sdk")
    if found:
        try:
            return ctypes.CDLL(found)
        except OSError:
            pass

    # 4. Try loading by name directly (relies on LD_LIBRARY_PATH / DYLD_LIBRARY_PATH)
    try:
        return ctypes.CDLL(lib_filename)
    except OSError:
        pass

    raise ImportError(
        format_missing_library_message(__version__, lib_filename)
    )


_lib = _load_library()
