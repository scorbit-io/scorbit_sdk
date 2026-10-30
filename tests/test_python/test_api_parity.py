# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Every public C function is reachable from the C++ API and from both Python packages.

The C API is the one surface every wrapper builds on, so a function the C headers declare but a
wrapper never calls is a feature that platform's integrators cannot use. Like
test_bindings_abi.py, this reads everything from source and needs no built library.
"""

import importlib.util
import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
HEADERS = os.path.join(ROOT, "include", "scorbit_sdk")
PYTHON_PACKAGES = {
    "py3": os.path.join(ROOT, "wrappers", "python", "src", "scorbit"),
    "py27": os.path.join(ROOT, "wrappers", "python27", "src", "scorbit"),
}

# CTest runs this with -I, which leaves this directory off sys.path.
_spec = importlib.util.spec_from_file_location(
    "test_bindings_abi", os.path.join(HERE, "test_bindings_abi.py")
)
_abi = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_abi)

# A platform that deliberately does not use a C function, and why. A function a platform does
# use must not be listed for it: the entry would hide a later regression.
EXCEPTIONS = {
    "sb_event_player_has_info": {
        "cpp": "PlayerInfo::hasInfo() gives the same answer from the id the C++ API already reads",
    },
    "sb_upload_diagnostics": {
        "cpp": "superseded by sb_upload_diagnostics_ex",
        "py3": "superseded by sb_upload_diagnostics_ex",
        "py27": "superseded by sb_upload_diagnostics_ex",
    },
}


def strip_comments(text, line_comment):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S) if line_comment == "//" else text
    return re.sub(re.escape(line_comment) + r"[^\n]*", " ", text)


def used(names, text, prefix, suffix=""):
    """The subset of *names* that *text* uses, as ``<prefix><name>``: called, or passed on as a
    function (an accessor handed to a helper, a destroy function given to a smart pointer)."""
    return {n for n in names if re.search(prefix + re.escape(n) + r"\b" + suffix, text)}


def cpp_calls(names):
    # The C++ headers only include the C prototypes, so any sb_* name in them is a use.
    text = ""
    for name in sorted(os.listdir(HEADERS)):
        if name.endswith(".h") and not name.endswith("_c.h"):
            with open(os.path.join(HEADERS, name)) as f:
                text += strip_comments(f.read(), "//")
    return used(names, text, r"(?<![\w.])")


def python_calls(names, package):
    text = ""
    for name in sorted(os.listdir(package)):
        if name.endswith(".py"):
            with open(os.path.join(package, name)) as f:
                text += strip_comments(f.read(), "#")
    # Its ctypes declaration (_lib.<name>.argtypes / .restype) is not a use.
    return used(names, text, r"\b_lib\.", r"(?!\s*\.\s*(?:argtypes|restype)\b)")


class TestEveryPlatformReachesTheCApi(unittest.TestCase):
    def setUp(self):
        self.functions = set(_abi.c_prototypes())
        self.coverage = {"cpp": cpp_calls(self.functions)}
        for platform, package in PYTHON_PACKAGES.items():
            self.coverage[platform] = python_calls(self.functions, package)

    def test_the_sources_are_read(self):
        # A parse that silently finds nothing would make every other check pass vacuously
        self.assertGreater(len(self.functions), 100)
        for platform, covered in self.coverage.items():
            with self.subTest(platform=platform):
                self.assertIn("sb_download", covered)
                self.assertGreater(len(covered), 100)

    def test_every_function_is_used_or_excepted(self):
        for platform, covered in sorted(self.coverage.items()):
            for name in sorted(self.functions - covered):
                with self.subTest(platform=platform, function=name):
                    self.assertIn(
                        platform,
                        EXCEPTIONS.get(name, {}),
                        "%s is declared in the C headers but %s never uses it; wrap it, or add "
                        "it to EXCEPTIONS with the reason" % (name, platform),
                    )

    def test_every_exception_is_still_needed(self):
        for name, platforms in sorted(EXCEPTIONS.items()):
            with self.subTest(function=name):
                self.assertIn(name, self.functions, "%s is no longer in the C headers" % name)
            for platform in sorted(platforms):
                with self.subTest(function=name, platform=platform):
                    self.assertNotIn(
                        name,
                        self.coverage[platform],
                        "%s now uses %s; drop its EXCEPTIONS entry" % (platform, name),
                    )


if __name__ == "__main__":
    unittest.main()
