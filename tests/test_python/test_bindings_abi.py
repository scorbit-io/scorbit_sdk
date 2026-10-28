# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Every ctypes declaration in the Python wrappers against the C prototype it binds.

ctypes trusts ``argtypes`` blindly: a declaration one argument short shifts every later
argument into the wrong slot, and the library crashes on the first call. Nothing else checks
the two stay in step, so this reads both from source; it needs no built library.
"""

import ast
import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HEADERS = os.path.join(ROOT, "include", "scorbit_sdk")
BINDINGS = [
    os.path.join(ROOT, "wrappers", "python", "src", "scorbit", "_bindings.py"),
    os.path.join(ROOT, "wrappers", "python27", "src", "scorbit", "_bindings.py"),
]

# A function prototype: its name and parameter list, which may hold one level of parentheses
# for a function-pointer parameter.
PROTOTYPE = re.compile(r"\b(sb_\w+)\s*\(((?:[^()]|\([^()]*\))*)\)\s*;")


def split_params(params):
    """Top-level comma-separated parameters of a C parameter list."""
    params = params.strip()
    if params in ("", "void"):
        return []
    parts, depth, current = [], 0, ""
    for ch in params:
        if ch == "," and depth == 0:
            parts.append(current)
            current = ""
            continue
        depth += {"(": 1, ")": -1}.get(ch, 0)
        current += ch
    parts.append(current)
    return parts


def c_prototypes():
    """``{name: parameter count}`` for every function the public C headers declare."""
    found = {}
    for name in sorted(os.listdir(HEADERS)):
        if not name.endswith("_c.h"):
            continue
        with open(os.path.join(HEADERS, name)) as f:
            text = f.read()
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        text = re.sub(r"//[^\n]*", " ", text)
        text = re.sub(r"^\s*#[^\n]*", " ", text, flags=re.M)
        for match in PROTOTYPE.finditer(text):
            found[match.group(1)] = len(split_params(match.group(2)))
    return found


def declared_argtypes(path):
    """``{name: (argument count, line)}`` for every ``_lib.<name>.argtypes = [...]``."""
    with open(path) as f:
        tree = ast.parse(f.read(), path)
    found = {}
    for node in ast.walk(tree):
        if not isinstance(node, ast.Assign) or not isinstance(node.value, ast.List):
            continue
        for target in node.targets:
            if (
                isinstance(target, ast.Attribute)
                and target.attr == "argtypes"
                and isinstance(target.value, ast.Attribute)
                and isinstance(target.value.value, ast.Name)
                and target.value.value.id == "_lib"
            ):
                found[target.value.attr] = (len(node.value.elts), node.lineno)
    return found


class TestBindingsMatchCPrototypes(unittest.TestCase):
    def test_the_headers_are_read(self):
        prototypes = c_prototypes()
        # A parse that silently finds nothing would make every other check pass vacuously
        self.assertGreater(len(prototypes), 50)
        self.assertEqual(prototypes["sb_config_create"], 0)
        self.assertEqual(prototypes["sb_download"], 7)

    def test_every_binding_has_the_prototypes_argument_count(self):
        prototypes = c_prototypes()
        for path in BINDINGS:
            declared = declared_argtypes(path)
            self.assertGreater(len(declared), 50, path)
            for name, (count, line) in sorted(declared.items()):
                with self.subTest(binding=os.path.relpath(path, ROOT), function=name):
                    self.assertIn(name, prototypes, "%s:%d binds a function no header declares"
                                  % (os.path.relpath(path, ROOT), line))
                    self.assertEqual(
                        count,
                        prototypes[name],
                        "%s:%d declares %d arguments for %s, the C prototype has %d"
                        % (os.path.relpath(path, ROOT), line, count, name, prototypes[name]),
                    )


if __name__ == "__main__":
    unittest.main()
