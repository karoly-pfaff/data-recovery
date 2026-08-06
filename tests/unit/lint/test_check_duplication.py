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


def _python_tree(name: str) -> list[Path]:
    return sorted((FIXTURES / name).glob("*.py"))


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
        sites = {Path(site.path).name: site for site in report.blocks[0].sites}
        spans = {
            name: (site.start_line, site.end_line) for name, site in sites.items()
        }
        # `readLittleEndian` occupies lines 9-16 of FirstParser.cc and 8-15 of
        # SecondParser.cc; the ranges are the match's, window edges and all.
        self.assertEqual(spans["FirstParser.cc"], (5, 14))
        self.assertEqual(spans["SecondParser.cc"], (4, 13))

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
    """Declarations rhyme; a block counts only where every site reaches code.

    Every byte parser in this tree opens with the same shape — an include list,
    a namespace, and a run of layout constants — because that is the only shape
    C++ offers for stating an on-disk offset. Those are different facts, and no
    refactoring makes them one, so a gate that reported them would be red for
    good.
    """

    # The rule is *reaches*, not *lies inside*, and this is the difference: a
    # match runs in windows of tokens, so the pair fixture's block opens on the
    # `#include` four lines above the function it is about. Under containment
    # the gate would report nothing at all — on the fixtures or on the tree.
    def test_a_site_that_begins_in_the_preamble_still_counts(self):
        report = check_duplication.duplicate_blocks(_tree("pair"), min_tokens=60)
        self.assertEqual(len(report.blocks), 1)
        first = next(
            site
            for site in report.blocks[0].sites
            if Path(site.path).name == "FirstParser.cc"
        )
        self.assertLess(first.start_line, 9)  # `readLittleEndian` starts at 9
        self.assertGreaterEqual(first.end_line, 9)

    def test_a_shared_declaration_shape_is_not_duplication(self):
        report = check_duplication.duplicate_blocks(
            _tree("declarations"), min_tokens=60
        )
        self.assertEqual(report.blocks, [])
        # And not because there was nothing to find: the rate is `lizard`'s own,
        # taken before this rule applies, so a non-zero one is the proof that
        # the two files *do* clone and that the rule is what rejected them.
        self.assertGreater(report.rate, 0.0)

    def test_a_declaration_family_is_rejected_even_where_the_file_holds_code(self):
        # The rule reads each site's own lines, not the file it sits in.
        report = check_duplication.duplicate_blocks(_tree("mixed"), min_tokens=60)
        self.assertEqual(report.blocks, [])
        self.assertGreater(report.rate, 0.0)



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


# story-0703: `tools/` was handed to this gate as a root while discovery
# admitted only `.cpp`/`.hpp`, so 3,796 lines of Python were measured by
# nothing. `lizard` tokenizes Python natively; the gate's two rules — a block
# counts at its per-copy length, and only where every site reaches a function
# body — are language-independent and are asserted here over Python.
class PythonTest(unittest.TestCase):
    def test_a_shared_python_block_is_reported_with_both_of_its_sites(self):
        report = check_duplication.duplicate_blocks(_python_tree("python"), min_tokens=40)
        self.assertEqual(len(report.blocks), 1)
        self.assertEqual(_named(report.blocks[0]), {"alpha.py", "beta.py"})

    def test_the_same_python_pair_passes_above_the_block(self):
        report = check_duplication.duplicate_blocks(_python_tree("python"), min_tokens=200)
        self.assertEqual(report.blocks, [])

    # The threshold is one number for both languages, chosen from a measurement
    # of each: the C++ median function is 61 tokens and the Python median 63,
    # both rounded down to 60. That they agree is a coincidence of this tree,
    # not an inheritance — this asserts the Python side reaches the bar.
    def test_the_python_block_reaches_the_shipped_threshold(self):
        report = check_duplication.duplicate_blocks(_python_tree("python"), min_tokens=60)
        self.assertEqual(len(report.blocks), 1)

    # story-0602's second rule — a block counts only where every site reaches a
    # function body — is a *C++* rule, and this is the case that settles it.
    # Applied to Python it hid a 153-token-per-copy module-level table
    # completely, because `lizard`'s `function_list` for a Python module holds
    # no range covering module scope. C++ needs the rule because an include
    # list and an offset table are the only shape it has for stating them;
    # Python has no preamble, so a table repeated in two modules is ordinary
    # refactorable duplication.
    def test_a_module_level_python_table_is_reported(self):
        report = check_duplication.duplicate_blocks(
            _python_tree("python-module-scope"), min_tokens=60
        )
        self.assertEqual(len(report.blocks), 1)
        self.assertEqual(_named(report.blocks[0]), {"gamma.py", "delta.py"})

    # The other half of the same decision: C++ preamble is still dropped.
    def test_a_cpp_declaration_family_is_still_dropped(self):
        report = check_duplication.duplicate_blocks(_tree("mixed"), min_tokens=40)
        self.assertEqual(report.blocks, [])


if __name__ == "__main__":
    unittest.main()
