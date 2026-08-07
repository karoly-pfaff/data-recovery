#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/lint/check_file_length.py`.

This gate enforces AGENTS.md §2's headline number — 250 lines — and it was the
only script in `tools/lint/` with no test of its own. story-0703 gave it Python
to measure, which is a good moment to also give it the coverage the others have.

The limit is stated language-independently in AGENTS.md ("File length (lines)
… 250"), which is why widening the gate to `.py` agrees with the contract as
written rather than amending it.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
GATE = REPO_ROOT / "tools" / "lint" / "check_file_length.py"


def run_gate(root: pathlib.Path, *, warn: int = 200, maximum: int = 250):
    return subprocess.run(
        [sys.executable, str(GATE), "--warn", str(warn), "--max", str(maximum), str(root)],
        capture_output=True,
        text=True,
        check=False,
    )


def write_lines(path: pathlib.Path, count: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join("x" for _ in range(count)) + "\n", encoding="utf-8")


class FileLengthGate(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_a_cpp_file_over_the_max_fails_and_names_itself(self):
        write_lines(self.root / "big.cpp", 300)
        outcome = run_gate(self.root)
        self.assertEqual(outcome.returncode, 1)
        self.assertIn("big.cpp", outcome.stderr)
        self.assertIn("300 lines", outcome.stderr)

    # The point of story-0703: `tools/` was handed to this gate as a root while
    # its discovery admitted only `.cpp` and `.hpp`, so 3,796 lines of Python
    # were measured by nothing. A 763-line file passed.
    def test_a_python_file_over_the_max_fails(self):
        write_lines(self.root / "big.py", 763)
        outcome = run_gate(self.root)
        self.assertEqual(outcome.returncode, 1)
        self.assertIn("big.py", outcome.stderr)

    def test_a_file_under_the_max_passes(self):
        write_lines(self.root / "small.py", 40)
        write_lines(self.root / "small.cpp", 40)
        self.assertEqual(run_gate(self.root).returncode, 0)

    def test_a_file_between_warn_and_max_warns_without_failing(self):
        write_lines(self.root / "middling.py", 220)
        outcome = run_gate(self.root)
        self.assertEqual(outcome.returncode, 0)
        self.assertIn("middling.py", outcome.stdout)

    def test_a_suffix_outside_the_contract_is_not_measured(self):
        write_lines(self.root / "notes.md", 4000)
        self.assertEqual(run_gate(self.root).returncode, 0)

    def test_a_missing_root_is_refused_rather_than_passed(self):
        outcome = run_gate(self.root / "absent")
        self.assertEqual(outcome.returncode, 2)


if __name__ == "__main__":
    unittest.main()
