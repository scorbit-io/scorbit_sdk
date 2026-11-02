# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Every public C function is reachable from the C++ API, both Python packages and the Godot add-on.

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
GODOT_SOURCES = os.path.join(ROOT, "wrappers", "godot", "src")
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
        "godot": "superseded by sb_upload_diagnostics_ex",
    },
    "sb_config_set_signer": {
        "godot": "the SDK calls its signer synchronously on its own thread, which GDScript can't "
        "serve; the add-on authenticates with encrypted_key",
    },
    "sb_download_with_options": {"godot": "internal use"},
}

# The Scorbitron device daemon's probe and self-update plumbing: no game calls these.
for _name in [
    "sb_config_add_probe_flash_probe",
    "sb_config_set_pbspkcomm_firmware_id",
    "sb_config_set_probe_firmware",
    "sb_config_set_probe_flash_nfc_trigger",
    "sb_config_set_scorbitd_platform_id",
    "sb_config_set_scorbitd_version",
    "sb_event_firmwares_list_received",
    "sb_event_hardware_probe_requested",
    "sb_event_probe_flash_requested",
    "sb_event_scorbitd_update_received",
    "sb_event_scorbitd_updated",
    "sb_set_pbspkcomm_firmware_id",
    "sb_set_probe_firmware",
    "sb_submit_hardware_probe_result",
    "sb_submit_probe_flash_result",
    "sb_submit_probe_flash_stage",
]:
    EXCEPTIONS.setdefault(_name, {})["godot"] = "Scorbitron device daemon plumbing"


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


def godot_calls(names):
    # The add-on's C++ calls the C API through a table of the functions it loads
    # (sdk_loader.h), which names each one.
    text = ""
    for name in sorted(os.listdir(GODOT_SOURCES)):
        if name.endswith((".cpp", ".h")):
            with open(os.path.join(GODOT_SOURCES, name)) as f:
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
        self.coverage = {"cpp": cpp_calls(self.functions), "godot": godot_calls(self.functions)}
        for platform, package in PYTHON_PACKAGES.items():
            self.coverage[platform] = python_calls(self.functions, package)

    def test_the_sources_are_read(self):
        # A parse that silently finds nothing would make every other check pass vacuously
        self.assertGreater(len(self.functions), 100)
        for platform, covered in self.coverage.items():
            with self.subTest(platform=platform):
                self.assertIn("sb_download", covered)
                # Godot uses fewest, about 98, by leaving out the device daemon's plumbing.
                self.assertGreater(len(covered), 90)

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
