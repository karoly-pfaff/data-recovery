#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/lint/adr_range.py`, over strings and one bare process.

`adr_document` got a direct unit module because its parsing is where the silent
passes came from. So is this one's: `split_range` and the `--name-status` reader
between them account for four of this story's defects, and until now both were
reachable only by building a git repository — three commits per case.

The refusal in `parse_name_status` is the sharper reason. Git emits only two-
and three-field lines, so no repository can produce the input that triggers it,
and a fault nothing can reach is a fault nothing has checked.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "tools" / "lint"))

from adr_markdown import CannotAnswer  # noqa: E402
from adr_range import Change, parse_name_status, split_range  # noqa: E402

GATE = pathlib.Path(__file__).resolve().parents[3] / "tools" / "lint" / "check_adr_immutability.py"


class ParseNameStatus(unittest.TestCase):
    def test_a_modification_carries_the_same_name_on_both_sides(self):
        self.assertEqual(
            parse_name_status("M\tdocs/architecture/adr/adr-0005-a.md"),
            [Change(kind="M", old="docs/architecture/adr/adr-0005-a.md",
                    new="docs/architecture/adr/adr-0005-a.md")],
        )

    def test_an_addition_has_no_old_name(self):
        [change] = parse_name_status("A\tdocs/architecture/adr/adr-0006-b.md")
        self.assertEqual(change.old, "")
        self.assertEqual(change.new, "docs/architecture/adr/adr-0006-b.md")

    def test_a_deletion_has_no_new_name(self):
        [change] = parse_name_status("D\tdocs/architecture/adr/adr-0005-a.md")
        self.assertEqual(change.old, "docs/architecture/adr/adr-0005-a.md")
        self.assertEqual(change.new, "")
        self.assertTrue(change.deleted)

    def test_a_rename_carries_both_names(self):
        [change] = parse_name_status("R096\told/adr-0005-a.md\tnew/adr-0005-a.md")
        self.assertEqual((change.old, change.new), ("old/adr-0005-a.md", "new/adr-0005-a.md"))
        self.assertFalse(change.deleted)

    # A type change (file to symlink) and an unmerged entry are both two-field,
    # and reading them as modifications is right: the record is still there.
    def test_a_type_change_reads_as_a_modification(self):
        [change] = parse_name_status("T\tdocs/architecture/adr/adr-0005-a.md")
        self.assertEqual(change.old, change.new)
        self.assertFalse(change.deleted)

    def test_several_lines_come_back_in_order(self):
        parsed = parse_name_status("A\tone.md\nD\ttwo.md\nR100\tthree.md\tfour.md\n")
        self.assertEqual([c.kind for c in parsed], ["A", "D", "R100"])

    def test_blank_lines_are_not_records(self):
        self.assertEqual(parse_name_status("\n\n"), [])

    # The branch no repository can reach. A gate whose thesis is that unreadable
    # input is a fault must not skip a line it cannot read — and until this file
    # existed, nothing could show that it does not.
    def test_a_line_it_cannot_read_is_a_fault(self):
        for line in ("R100\ta\tb\tc", "M", "\tno-status"):
            with self.subTest(line=line):
                with self.assertRaises(CannotAnswer) as refused:
                    parse_name_status(line)
                self.assertIn("cannot read this line", str(refused.exception))


class SplitRange(unittest.TestCase):
    def test_two_dots(self):
        self.assertEqual(split_range("a..b"), ("a", "..", "b"))

    def test_three_dots(self):
        self.assertEqual(split_range("main...HEAD"), ("main", "...", "HEAD"))

    # `git diff ..b` and `git diff a..` both mean HEAD on the empty side.
    def test_an_omitted_side_means_head(self):
        self.assertEqual(split_range("..b"), ("HEAD", "..", "b"))
        self.assertEqual(split_range("a.."), ("a", "..", "HEAD"))

    def test_a_bare_commit_is_not_a_range(self):
        with self.assertRaises(CannotAnswer) as refused:
            split_range("HEAD")
        self.assertIn("not a range", str(refused.exception))


class OutsideARepository(unittest.TestCase):
    """`git rev-parse --show-toplevel` fails, and that is a fault, not a pass.

    Worth its own case because the gate now *depends* on resolving the top
    level: it is the fix for the pathspec that matched nothing from a
    subdirectory, so the failure mode of that fix needs to be the right one.
    """

    def test_the_gate_refuses_rather_than_reporting_a_clean_pass(self):
        with tempfile.TemporaryDirectory() as elsewhere:
            outcome = subprocess.run(
                [sys.executable, str(GATE), "HEAD~1..HEAD"],
                cwd=elsewhere, capture_output=True, text=True, check=False, encoding="utf-8",
            )
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("not inside a git repository", outcome.stderr)


if __name__ == "__main__":
    unittest.main()
