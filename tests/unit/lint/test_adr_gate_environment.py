#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What the repository and the shell can do to the gate's reading of git.

Split from `test_adr_gate_ranges.py`, which asks what a *range* changed. These
ask what happens when git is told to answer differently: an attribute file, a
diff driver, a textconv filter, the working directory. Every case here made the
gate exit 0 on `4a4221e`, and each is answered by one entry in
`adr_range.DIFF_FLAGS` or by running git from the repository root.
"""
from __future__ import annotations

import sys
import unittest

from adr_fixture import ADR_DIR, AdrGateTest, git


class WhatTheRepositoryCanTellGitToDo(AdrGateTest):
    """A gate whose verdict moves with config or cwd is not a gate.

    Each case below made the gate exit **0** on an edit it must catch, and two
    of them were reproduced against the real `4a4221e`. Each is answered by one
    flag in `adr_range.DIFF_FLAGS`, or by running git from the top level;
    remove that one thing and the matching test here fails.
    """

    def rewrite_the_decision(self) -> None:
        self.edit("Decision", "Rewritten in place.")
        self.repo.commit("rewrite the decision")
        self.assertEqual(self.gate().returncode, 1, "the fixture must fail to begin with")

    # `--text`. One in-tree `.gitattributes` line and `git diff` prints
    # "Binary files differ" with no `@@` headers at all, so every hunk set comes
    # back empty. No user configuration is involved: the file is committed, and
    # nothing else in the tree inspects it.
    def test_an_attribute_marking_the_adrs_binary_does_not_hide_the_edit(self):
        self.rewrite_the_decision()
        (self.repo.root / ".gitattributes").write_text(
            "docs/architecture/adr/*.md -diff\n", encoding="utf-8"
        )
        self.repo.commit("mark the ADRs as binary")
        outcome = self.gate("HEAD~2..HEAD")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    # `--no-ext-diff`. An external driver replaces the patch wholesale.
    def test_an_external_diff_driver_does_not_hide_the_edit(self):
        self.rewrite_the_decision()
        git(self.repo.root, "config", "diff.external", "true")
        # The fixture has to actually break git, or this passes either way: a
        # driver the gate's flags stop git from ever invoking proves nothing
        # about the flags. Assert the raw patch is mangled first.
        raw = git(self.repo.root, "diff", "--unified=0", "HEAD~1..HEAD")
        self.assertNotIn("@@", raw, "diff.external did not take effect")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)

    # `--no-textconv`. A filter that drops lines renumbers the whole file, so
    # the hunk headers name lines that are in a different section — or in none.
    def test_a_textconv_filter_does_not_move_the_line_numbers(self):
        stripper = self.repo.root / "strip.py"
        stripper.write_text(
            "import sys\n"
            "print(''.join(l for l in open(sys.argv[1], encoding='utf-8') if l.strip()))\n",
            encoding="utf-8",
        )
        git(
            self.repo.root, "config", "diff.stripped.textconv",
            f'"{sys.executable}" "{stripper.as_posix()}"',
        )
        (self.repo.root / ".gitattributes").write_text(
            "docs/architecture/adr/*.md diff=stripped\n", encoding="utf-8"
        )
        self.repo.commit("install a textconv filter")
        self.edit("Decision", "Rewritten in place.")
        self.repo.commit("rewrite the decision")
        # Again: prove the filter runs and renumbers, or the test is green
        # whether or not the flag that defeats it is there. Stripping the blank
        # lines moves every hunk header, which is exactly the damage.
        plain = git(self.repo.root, "diff", "--no-textconv", "--unified=0", "HEAD~1..HEAD")
        filtered = git(self.repo.root, "diff", "--unified=0", "HEAD~1..HEAD")
        self.assertNotEqual(
            [line for line in plain.splitlines() if line.startswith("@@")],
            [line for line in filtered.splitlines() if line.startswith("@@")],
            "the textconv filter did not run, so this proves nothing",
        )
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    # `-M`. With renames off, a pure slug correction is reported as a delete
    # plus an add, and fails as "deleted while Accepted".
    def test_rename_detection_being_off_does_not_invent_a_deletion(self):
        git(self.repo.root, "config", "diff.renames", "false")
        self.rename_to("adr-0005-a-better-slug.md")
        self.repo.commit("pure rename")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 0, outcome.stdout + outcome.stderr)

    # The pathspec is relative to the working directory, so from anywhere but
    # the root it matched nothing — while the range stayed non-empty, so the
    # vacuity guard was satisfied and the gate reported a clean pass. This one
    # needed no configuration at all, only a different `cd`.
    def test_running_from_a_subdirectory_does_not_hide_the_edit(self):
        self.rewrite_the_decision()
        elsewhere = self.repo.root / ADR_DIR
        outcome = self.gate(cwd=elsewhere)
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)


if __name__ == "__main__":
    unittest.main()
