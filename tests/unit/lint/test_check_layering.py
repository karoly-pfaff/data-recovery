#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/lint/check_layering.py`.

The fixtures are built in a temp directory rather than checked in: a tree of
deliberately wrong `.cpp` files under `tests/` would be swept into the format
and file-length gates' own roots and fail them instead.

Run by ctest alongside the other gate tests; `python3 -m unittest` from the
repository root works too.
"""
from __future__ import annotations

import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "lint"))

import check_layering  # noqa: E402


def _write(root: Path, relative: str, body: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(body, encoding="utf-8")
    return path


class GateTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def run_gate(self, *roots: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        argv = sys.argv
        sys.argv = ["check_layering.py", *(str(self.root / r) for r in roots)]
        try:
            with redirect_stdout(out), redirect_stderr(err):
                code = check_layering.main()
        finally:
            sys.argv = argv
        return code, out.getvalue(), err.getvalue()

    def test_an_upward_include_fails_naming_file_line_header_and_edge(self):
        _write(self.root, "src/volume/GptEntry.cpp", '#include "revenant/fs/NameDecode.hpp"\n')
        code, _, err = self.run_gate("src")
        self.assertEqual(code, 1)
        # All three, because a verdict that names only the file sends the reader
        # hunting for which of its lines is the offending one.
        self.assertIn("GptEntry.cpp:1:", err)
        self.assertIn("volume/ must not include fs/", err)
        self.assertIn("revenant/fs/NameDecode.hpp", err)

    def test_both_spellings_of_one_header_are_the_same_edge(self):
        _write(self.root, "src/volume/A.cpp", '#include "revenant/fs/NameDecode.hpp"\n')
        _write(self.root, "src/volume/B.cpp", '#include "fs/NameDecode.hpp"\n')
        code, _, err = self.run_gate("src")
        self.assertEqual(code, 1)
        # The prefixed spelling is what hid GptEntry.cpp from a naive reading.
        self.assertIn("A.cpp:1:", err)
        self.assertIn("B.cpp:1:", err)

    def test_downward_and_same_layer_includes_pass(self):
        _write(self.root, "src/cli/Skip.cpp", '#include "core/Result.hpp"\n')
        _write(self.root, "src/fs/Adjacent.cpp", '#include "volume/Gpt.hpp"\n')
        _write(self.root, "src/carve/Same.cpp", '#include "carve/Signature.hpp"\n')
        code, out, _ = self.run_gate("src")
        self.assertEqual(code, 0)
        self.assertIn("cross-layer edges", out)

    def test_system_and_third_party_includes_are_not_edges(self):
        _write(
            self.root,
            "src/core/Plain.cpp",
            '#include <vector>\n#include "nlohmann/json.hpp"\n',
        )
        code, out, _ = self.run_gate("src")
        self.assertEqual(code, 0)
        self.assertIn("0 cross-layer edges", out)

    def test_a_commented_out_include_is_not_an_include(self):
        _write(self.root, "src/volume/Commented.cpp", '// #include "fs/NameDecode.hpp"\n')
        code, _, _ = self.run_gate("src")
        self.assertEqual(code, 0)

    def test_nothing_in_src_may_include_the_fixture_builders(self):
        _write(self.root, "src/fs/Cheat.cpp", '#include "imagegen/ntfs/Layout.hpp"\n')
        code, _, err = self.run_gate("src")
        self.assertEqual(code, 1)
        self.assertIn("fs/ must not include tools/", err)

    def test_an_undeclared_directory_stops_the_gate_naming_it(self):
        _write(self.root, "src/telemetry/New.cpp", "// nothing\n")
        with self.assertLogs(level="ERROR") as captured:
            code, _, _ = self.run_gate("src")
        # Exit 2, not 1: this is a configuration bug, not a violation. A new
        # layer nobody declared must stop the build rather than be skipped.
        self.assertEqual(code, 2)
        self.assertTrue(any("telemetry" in line for line in captured.output))

    def test_every_violation_is_reported_not_only_the_first(self):
        _write(self.root, "src/volume/One.cpp", '#include "fs/A.hpp"\n')
        _write(self.root, "src/volume/Two.cpp", '#include "carve/B.hpp"\n')
        _write(self.root, "src/core/Three.cpp", '#include "cli/C.hpp"\n')
        code, _, err = self.run_gate("src")
        self.assertEqual(code, 1)
        # A gate that stops at the first turns a cleanup into a queue.
        self.assertIn("One.cpp", err)
        self.assertIn("Two.cpp", err)
        self.assertIn("Three.cpp", err)
        self.assertIn("3 upward include(s)", err)

    def test_a_missing_root_refuses_to_pass(self):
        with self.assertLogs(level="ERROR") as captured:
            code, _, _ = self.run_gate("nope")
        self.assertEqual(code, 2)
        self.assertTrue(any("does not exist" in line for line in captured.output))

    # A gate that matches nothing must refuse to pass rather than pass
    # vacuously — the rule story-0607 gave the format gate, applied here.
    def test_an_empty_file_set_refuses_to_pass(self):
        (self.root / "src").mkdir()
        with self.assertLogs(level="ERROR") as captured:
            code, _, _ = self.run_gate("src")
        self.assertEqual(code, 2)
        self.assertTrue(any("empty gate" in line for line in captured.output))

    def test_a_public_header_is_placed_by_its_layer_not_its_root(self):
        _write(
            self.root,
            "include/revenant/volume/Gpt.hpp",
            '#include "revenant/fs/Types.hpp"\n',
        )
        code, _, err = self.run_gate("include")
        self.assertEqual(code, 1)
        self.assertIn("volume/ must not include fs/", err)


if __name__ == "__main__":
    unittest.main()
