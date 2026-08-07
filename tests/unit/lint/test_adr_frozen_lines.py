#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Which lines belong to a frozen section — the arithmetic under the rule.

Split from `test_check_adr_immutability.py`, which asks what the rule *says*.
These ask whether the gate and git agree about where the section is at all. Two
ways they did not: a character `str.splitlines()` breaks on and git does not,
and a heading inserted underneath that moves the boundary out from under the
text. Both let an Accepted Decision be rewritten with the gate green.
"""
from __future__ import annotations

import unittest

from adr_fixture import AdrGateTest


class LinesGitDoesNotCount(AdrGateTest):
    """`str.splitlines()` breaks on more characters than git does.

    U+2028, U+2029, U+0085, a vertical tab, a form feed, U+001C-U+001E — git
    counts the newline alone. One of them above a frozen heading shifted every
    computed span past the hunk numbers git reports, so the top of the Decision
    fell outside its own section and could be rewritten with the gate green. No
    intent required: a form feed pasted out of a word processor does it, and
    `docs/` is outside the encoding gate's roots, so nothing upstream rejects
    the character either.
    """

    def rewrite_beneath(self, separator: str):
        self.substitute("Why.", separator.join(["Why", "so", "very", "much."]))
        self.repo.commit("a paste from somewhere else")
        self.substitute("The decision, as accepted.", "Reversed entirely.")
        self.repo.commit("rewrite the decision")
        return self.gate()

    def test_a_unicode_line_separator_does_not_shift_the_frozen_span(self):
        for name, separator in (
            ("U+2028 line separator", "\u2028"),
            ("U+2029 paragraph separator", "\u2029"),
            ("form feed", "\f"),
            ("U+0085 next line", "\u0085"),
        ):
            with self.subTest(separator=name):
                self.reset()
                outcome = self.rewrite_beneath(separator)
                self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
                self.assertIn("Decision", outcome.stderr)


class HeadingsInsertedUnderneath(AdrGateTest):
    """A frozen section cannot be emptied from below by a new sibling heading.

    `## Notes` inserted directly beneath `## Decision` ends the Decision span at
    its own heading; the inserted lines then belong to `## Notes`, nothing is
    reported, and the decision text sits outside every frozen section, free to
    rewrite in the next change. Both pull requests were green.

    The cause is the mirror of the deletion defect fixed earlier: a pure
    insertion covers no *old* line, so the old side was blind to it exactly as
    the new side had been blind to a pure removal.
    """

    def test_inserting_a_heading_inside_a_frozen_section_is_the_breach(self):
        self.substitute("## Decision\n", "## Decision\n\n## Notes\n")
        self.repo.commit("add a Notes heading")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    def test_appending_to_a_frozen_section_is_still_the_breach(self):
        # The same blindness at the end of the file: an append covers no old
        # line either, and the last frozen section runs to the end.
        self.repo.write(
            "adr-0005-a-decision.md", self.repo.read() + "- And one more thing.\n"
        )
        self.repo.commit("append a consequence")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Consequences", outcome.stderr)

    def test_inserting_into_the_context_still_passes(self):
        self.substitute("Why.\n", "Why.\n\nAnd a fuller account.\n")
        self.repo.commit("expand the context")
        self.assertEqual(self.gate().returncode, 0)


if __name__ == "__main__":
    unittest.main()
