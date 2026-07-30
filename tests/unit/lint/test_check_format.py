#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the format gate driver in `tools/lint/check_format.py`.

clang-format's verdict on real bytes belongs to clang-format; what belongs to
the driver — and to these tests — is everything story-0607 exists for: the file
set is discovered at run time, the command lines it builds stay under a stated
budget no matter how the tree grows, a violation in any batch fails the whole
gate naming the file, and an empty match refuses to pass.

Run by ctest alongside the other gate tests; `python3 -m unittest` from the
repository root works too.
"""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "lint"))

import check_format  # noqa: E402


def _touch(root: Path, relative: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("// fixture\n", encoding="utf-8")
    return path


class DiscoveryTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_finds_cpp_and_hpp_under_every_root_sorted(self):
        beta = _touch(self.root, "src/beta.cpp")
        alpha = _touch(self.root, "include/alpha.hpp")
        found = check_format.source_files([self.root / "src", self.root / "include"])
        self.assertEqual(found, sorted([alpha, beta]))

    def test_ignores_suffixes_the_naming_contract_forbids(self):
        _touch(self.root, "src/notes.md")
        _touch(self.root, "src/build.py")
        _touch(self.root, "src/legacy.h")
        kept = _touch(self.root, "src/kept.hpp")
        self.assertEqual(check_format.source_files([self.root / "src"]), [kept])

    # A typo'd root would quietly shrink the gate's coverage — a gate that
    # checks less than it claims must stop, not pass (the shard-validation
    # rule in DevTargets.cmake, applied per root).
    def test_a_missing_root_is_refused(self):
        _touch(self.root, "src/kept.cpp")
        with self.assertRaises(FileNotFoundError):
            check_format.source_files([self.root / "src", self.root / "absent"])


class BatchingTest(unittest.TestCase):
    """The command line died of its own length once; the budget is the cure."""

    def test_every_file_lands_in_exactly_one_batch_in_order(self):
        files = [Path(f"dir/file{i:03}.cpp") for i in range(100)]
        batches = check_format.batch_by_length(files, budget=2000)
        flattened = [path for batch in batches for path in batch]
        self.assertEqual(flattened, files)

    def test_no_batch_exceeds_the_budget(self):
        files = [Path(f"dir/a-rather-long-file-name-{i:04}.cpp") for i in range(200)]
        budget = 500
        for batch in check_format.batch_by_length(files, budget=budget):
            joined = sum(len(str(path)) + 1 for path in batch)
            self.assertLessEqual(joined, budget)

    def test_growth_adds_batches_not_length(self):
        small = check_format.batch_by_length(
            [Path(f"f{i}.cpp") for i in range(10)], budget=64
        )
        large = check_format.batch_by_length(
            [Path(f"f{i}.cpp") for i in range(1000)], budget=64
        )
        self.assertGreater(len(large), len(small))

    def test_a_path_longer_than_the_budget_still_travels_alone(self):
        oversized = Path("deep/" * 60) / "leaf.cpp"
        batches = check_format.batch_by_length([oversized], budget=64)
        self.assertEqual(batches, [[oversized]])


class VerdictTest(unittest.TestCase):
    """The gate's exit code, with the subprocess behind an injected seam."""

    @staticmethod
    def _files(count):
        return [Path(f"src/file{i}.cpp") for i in range(count)]

    def test_a_clean_tree_passes(self):
        outcome = check_format.run_gate(
            self._files(3), fix=False, budget=10_000, runner=lambda argv: (0, "")
        )
        self.assertEqual(outcome, 0)

    def test_one_bad_batch_fails_the_gate_and_names_the_file(self):
        def runner(argv):
            if any("file4" in arg for arg in argv):
                return (1, "src/file4.cpp:1:1: error: code should be clang-formatted")
            return (0, "")

        with self.assertLogs(level="ERROR") as captured:
            outcome = check_format.run_gate(
                self._files(6), fix=False, budget=64, runner=runner
            )
        self.assertEqual(outcome, 1)
        self.assertTrue(any("file4" in line for line in captured.output))

    def test_every_batch_runs_even_after_a_failure(self):
        seen = []

        def runner(argv):
            seen.extend(arg for arg in argv if arg.endswith(".cpp"))
            return (1, "error")

        check_format.run_gate(self._files(8), fix=False, budget=64, runner=runner)
        self.assertEqual(len(seen), 8)

    def test_check_mode_asks_for_a_dry_run_and_fix_mode_for_in_place(self):
        commands = []

        def runner(argv):
            commands.append(argv)
            return (0, "")

        check_format.run_gate(self._files(1), fix=False, budget=10_000, runner=runner)
        check_format.run_gate(self._files(1), fix=True, budget=10_000, runner=runner)
        self.assertIn("--dry-run", commands[0])
        self.assertIn("--Werror", commands[0])
        self.assertIn("-i", commands[1])
        self.assertNotIn("--dry-run", commands[1])

    # A gate that matches nothing must refuse to pass, not pass vacuously —
    # the coverage gate's rule, applied here.
    def test_an_empty_file_set_is_refused(self):
        outcome = check_format.run_gate(
            [], fix=False, budget=10_000, runner=lambda argv: (0, "")
        )
        self.assertNotEqual(outcome, 0)


if __name__ == "__main__":
    unittest.main()
