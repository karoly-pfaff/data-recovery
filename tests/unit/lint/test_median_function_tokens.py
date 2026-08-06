#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/lint/median_function_tokens.py`.

This script exists because the command it replaced — a one-liner in
`docs/testing/quality-gates.md` — computed nothing and exited 0, so a reader
believed a number was checkable when nothing checked it. A script written for
that reason and then left untested is the same defect one level up, which is
what the self-audit pointed out: story-0703 gave `check_file_length.py` its
first test and created the next untested script in the same commit.

The behaviour worth pinning is the refusal: a median over no functions is not a
measurement, and must not print a number.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "tools" / "lint" / "median_function_tokens.py"

sys.path.insert(0, str(REPO_ROOT / "tools" / "lint"))

import median_function_tokens  # noqa: E402
from source_set import CPP_SUFFIXES, PYTHON_SUFFIXES  # noqa: E402


def run_script(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True,
        text=True,
        check=False,
        cwd=REPO_ROOT,
    )


class MedianFunctionTokens(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def write(self, name: str, text: str) -> None:
        (self.root / name).write_text(text, encoding="utf-8")

    # The guard the script exists for: a tree with files but no functions is not
    # a zero-token median, it is nothing to measure.
    def test_a_root_with_no_functions_refuses_rather_than_reporting(self):
        self.write("constants.py", "ALPHA = 1\nBETA = 2\n")
        with self.assertRaises(SystemExit) as refused:
            median_function_tokens.median_tokens([str(self.root)], PYTHON_SUFFIXES)
        self.assertIn("not a measurement", str(refused.exception))

    def test_it_measures_the_functions_it_is_given(self):
        self.write(
            "two.py",
            "def one(a):\n    return a + 1\n\n\ndef two(a, b):\n    return a + b\n",
        )
        files, functions, median = median_function_tokens.median_tokens(
            [str(self.root)], PYTHON_SUFFIXES
        )
        self.assertEqual(files, 1)
        self.assertEqual(functions, 2)
        self.assertGreater(median, 0)

    # The language argument selects the suffix set; a Python root measured as
    # C++ finds nothing, which is the refusal above rather than a silent zero.
    def test_the_language_selects_the_suffix_set(self):
        self.write("only.py", "def one(a):\n    return a + 1\n")
        with self.assertRaises(SystemExit):
            median_function_tokens.median_tokens([str(self.root)], CPP_SUFFIXES)

    # End to end, over the real tree: it must print a number and exit 0. This is
    # the invocation `quality-gates.md` documents, and running it is the whole
    # point — the command it replaced exited 0 with no output.
    def test_the_documented_invocation_prints_a_median(self):
        for language in ("cpp", "python"):
            with self.subTest(language=language):
                outcome = run_script(language)
                self.assertEqual(outcome.returncode, 0, outcome.stderr)
                self.assertIn(f"{language}:", outcome.stdout)
                self.assertIn("median", outcome.stdout)
                self.assertRegex(outcome.stdout, r"median \d+")

    def test_an_unknown_language_is_refused(self):
        outcome = run_script("rust")
        self.assertNotEqual(outcome.returncode, 0)


if __name__ == "__main__":
    unittest.main()
