#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the duplication gate in `tools/lint/check_duplication.py`.

Finding clones in C++ belongs to `lizard`; what belongs to the driver — and to
these tests — is the verdict built on top of it: which blocks count at a given
token threshold, that a family is reported once with all its sites rather than
once per site, and that an empty file set cannot pass.

The fixtures are `.cc`, not `.cpp`. The gates that walk the tree take their file
set from `tools/lint/source_set.py` and from the `*.cpp`/`*.hpp` glob in
`cmake/DevTargets.cmake`, so a deliberately duplicated fixture outside the
naming contract (AGENTS.md §1) is invisible to them — it is gate input, like the
JSON under `tests/fixtures/coverage/`. The pre-commit hook is the exception: it
formats staged `.cc` and `.h` too, so these files are clang-format clean.

Run by ctest alongside the other gate tests; `python3 -m unittest` from the
repository root works too.
"""
from __future__ import annotations

import contextlib
import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "lint"))

import check_duplication  # noqa: E402

FIXTURES = Path(__file__).resolve().parents[2] / "fixtures" / "duplication"


def _tree(name: str) -> list[Path]:
    return sorted((FIXTURES / name).glob("*.cc"))


def _named(block) -> set[str]:
    return {Path(site.path).name for site in block.sites}


class ReportingTest(unittest.TestCase):
    # The token count is asserted exactly, not against the threshold that
    # selected it: the gate reads a block's length off `lizard`'s own node
    # indices, and if that arithmetic drifts under a version bump, 60 quietly
    # starts meaning something else. This is the assertion that notices.
    def test_a_shared_block_is_reported_with_both_of_its_sites(self):
        report = check_duplication.duplicate_blocks(_tree("pair"), min_tokens=40)
        self.assertEqual(len(report.blocks), 1)
        block = report.blocks[0]
        self.assertEqual(_named(block), {"FirstParser.cc", "SecondParser.cc"})
        self.assertEqual(block.tokens, 99)

    def test_each_site_names_the_line_range_it_covers(self):
        report = check_duplication.duplicate_blocks(_tree("pair"), min_tokens=40)
        for site in report.blocks[0].sites:
            self.assertLessEqual(site.start_line, site.end_line)
            # The shared decoder, not the file's preamble.
            self.assertGreater(site.start_line, 1)

    def test_the_same_pair_passes_under_a_threshold_above_the_block(self):
        report = check_duplication.duplicate_blocks(_tree("pair"), min_tokens=100)
        self.assertEqual(report.blocks, [])

    def test_a_tree_without_duplication_passes(self):
        report = check_duplication.duplicate_blocks(_tree("clean"), min_tokens=40)
        self.assertEqual(report.blocks, [])

    def test_a_block_with_three_homes_is_one_block_with_three_sites(self):
        report = check_duplication.duplicate_blocks(_tree("triple"), min_tokens=40)
        self.assertEqual(len(report.blocks), 1)
        self.assertEqual(
            _named(report.blocks[0]),
            {"AlphaStore.cc", "BetaStore.cc", "GammaStore.cc"},
        )

    def test_the_rate_is_reported_beside_the_blocks(self):
        duplicated = check_duplication.duplicate_blocks(_tree("pair"), min_tokens=40)
        clean = check_duplication.duplicate_blocks(_tree("clean"), min_tokens=40)
        self.assertGreater(duplicated.rate, 0.0)
        self.assertEqual(clean.rate, 0.0)


class PerCopyThresholdTest(unittest.TestCase):
    """The threshold is what each copy must reach, not what they reach together.

    `lizard`'s own `min_duplicate_tokens` counts the tokens across every copy,
    so a large family of short blocks clears a threshold no single copy comes
    near. On this tree that is include lists and interface declarations — the
    shapes a DRY rule is not about.
    """

    def test_a_family_of_short_copies_does_not_reach_a_higher_threshold(self):
        report = check_duplication.duplicate_blocks(_tree("family"), min_tokens=70)
        self.assertEqual(report.blocks, [])

    def test_the_same_family_is_found_when_the_threshold_is_below_a_copy(self):
        report = check_duplication.duplicate_blocks(_tree("family"), min_tokens=40)
        families = [block for block in report.blocks if len(block.sites) == 4]
        self.assertEqual(len(families), 1)
        self.assertLess(families[0].tokens, 70)


class CodeOnlyTest(unittest.TestCase):
    """Declarations rhyme; only code counts.

    Every byte parser in this tree opens with the same shape — an include list,
    a namespace, and a run of layout constants — because that is the only shape
    C++ offers for stating an on-disk offset. Those are different facts, and no
    refactoring makes them one, so a gate that reported them would be red for
    good.
    """

    def test_a_shared_declaration_shape_is_not_duplication(self):
        report = check_duplication.duplicate_blocks(
            _tree("declarations"), min_tokens=60
        )
        self.assertEqual(report.blocks, [])
        # And not because there was nothing to find: the rate is `lizard`'s own,
        # taken before this rule applies, so a non-zero one is the proof that
        # the two files *do* clone and that the rule is what rejected them.
        self.assertGreater(report.rate, 0.0)

    def test_the_same_shape_inside_functions_still_is(self):
        # The companion to the case above: `pair` is found at the same
        # threshold, so the pass there is the rule and not an empty scan.
        report = check_duplication.duplicate_blocks(_tree("pair"), min_tokens=60)
        self.assertEqual(len(report.blocks), 1)


class VerdictTest(unittest.TestCase):
    def test_a_clean_tree_exits_zero(self):
        self.assertEqual(check_duplication.run_gate(_tree("clean"), min_tokens=40), 0)

    def test_a_duplicated_tree_fails_naming_both_files(self):
        with self.assertLogs(level="ERROR") as captured:
            outcome = check_duplication.run_gate(_tree("pair"), min_tokens=40)
        self.assertEqual(outcome, 1)
        logged = "\n".join(captured.output)
        self.assertIn("FirstParser.cc", logged)
        self.assertIn("SecondParser.cc", logged)

    # A gate that matches nothing must refuse to pass, not pass vacuously —
    # the coverage gate's rule, applied here.
    def test_an_empty_file_set_is_refused(self):
        self.assertNotEqual(check_duplication.run_gate([], min_tokens=40), 0)

    # The rate has to reach a human, not just the return value: a trend nobody
    # can read is not a trend.
    def test_the_verdict_line_carries_the_count_and_the_rate(self):
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            check_duplication.run_gate(_tree("clean"), min_tokens=40)
        line = printed.getvalue()
        self.assertIn("0 block(s) at or above 40 tokens per copy", line)
        self.assertIn("duplicate rate 0.00%", line)


if __name__ == "__main__":
    unittest.main()
