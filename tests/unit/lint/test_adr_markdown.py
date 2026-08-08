#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/lint/adr_markdown.py` — which bytes a reader sees.

Three audit rounds found defects in this layer and not one was about ADRs:
fenced blocks, then nesting, then HTML comments. It is the bottom of the stack
for that reason, and it is tested on its own for the same one.
"""
from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "tools" / "lint"))

from adr_document import names_as_superseded  # noqa: E402
from adr_markdown import CannotAnswer, visible_prose  # noqa: E402


class VisibleProse(unittest.TestCase):
    """What a reader sees speaks for the record; nothing else does.

    Fences were blanked first, then nesting. Comments were a hole rather than a
    refinement: every ADR opens with one, so hidden text was already idiomatic
    in every file in the tree.
    """

    def test_a_comment_is_blanked_and_the_line_count_kept(self):
        text = "a\n<!--\n**Status:** Proposed\n-->\nb\n"
        self.assertEqual(visible_prose(text).splitlines(), ["a", "", "", "", "b"])

    def test_a_comment_opened_and_closed_on_one_line_leaves_the_rest(self):
        self.assertEqual(visible_prose("keep <!-- drop --> this").strip(), "keep  this")

    # Removed, not replaced by a space: a renderer joins what a comment
    # separates, so `Acc<!-- -->epted` is one word to every reader. Replacing it
    # with a space made the gate read `Acc`, which is not a status it knows —
    # and "a status it does not know" used to mean "not frozen".
    def test_a_comment_inside_a_word_does_not_split_it(self):
        self.assertEqual(visible_prose("Acc<!-- -->epted"), "Accepted")

    def test_an_unclosed_comment_is_refused_rather_than_swallowing_the_file(self):
        with self.assertRaises(CannotAnswer) as refused:
            visible_prose("a\n<!-- never closed\nb\n", "adr-0005-a.md")
        self.assertIn("never closed", str(refused.exception))
        self.assertIn("adr-0005-a.md", str(refused.exception))

    # A comment inside a fence is part of the example, not a comment.
    def test_a_comment_inside_a_fence_does_not_leak_out_of_it(self):
        text = "a\n```\n<!-- unclosed inside the example\n```\nb\n"
        self.assertEqual(visible_prose(text).splitlines(), ["a", "", "", "", "b"])


class Fences(unittest.TestCase):
    def test_a_fenced_block_is_blanked_and_the_line_count_is_kept(self):
        text = "one\n```\ntwo\nthree\n```\nfour\n"
        # Blanked rather than removed: the line numbers have to keep matching a
        # diff's, and every hunk header the gate reads counts from the top.
        self.assertEqual(visible_prose(text), "one\n\n\n\n\nfour")
        self.assertEqual(len(visible_prose(text).splitlines()), 6)

    def test_a_tilde_fence_counts_as_a_fence(self):
        self.assertEqual(visible_prose("a\n~~~\nb\n~~~\nc"), "a\n\n\n\nc")

    def test_an_indented_fence_counts_as_a_fence(self):
        self.assertEqual(visible_prose("a\n  ```\n  b\n  ```\nc"), "a\n\n\n\nc")

    # An ADR documenting the ADR template is exactly this shape, and a boolean
    # toggled by any fence line re-opens the outer block at the first inner one
    # — un-blanking the example, so a `**Supersedes:**` inside it would read as
    # a declaration. That is the defect fencing was introduced to close.
    def test_a_fence_inside_a_longer_fence_stays_fenced(self):
        text = (
            "before\n````markdown\n"
            "- **Supersedes:** [ADR-0005](adr-0005-a.md)\n"
            "```\nnested\n```\n"
            "## Decision\n"
            "````\nafter\n"
        )
        kept = visible_prose(text)
        self.assertEqual(kept.splitlines()[0], "before")
        self.assertEqual(kept.splitlines()[-1], "after")
        self.assertNotIn("Supersedes", kept)
        self.assertNotIn("Decision", kept)
        self.assertFalse(names_as_superseded(text, "0005"))

    def test_an_odd_number_of_inner_fences_is_not_a_false_imbalance(self):
        # Two closers of the wrong length inside a ````-fenced block: the outer
        # block is still balanced, so this is a well-formed document.
        text = "a\n````\n```\n```\n```\n````\nb\n"
        self.assertEqual(visible_prose(text).splitlines(), ["a", "", "", "", "", "", "b"])

    def test_an_unclosed_fence_is_refused_rather_than_blanking_the_tail(self):
        with self.assertRaises(CannotAnswer) as refused:
            visible_prose("a\n```\nb\n", "adr-0005-a-decision.md")
        self.assertIn("never closed", str(refused.exception))
        self.assertIn("adr-0005-a-decision.md", str(refused.exception))

    def test_text_with_no_fence_keeps_every_line_where_it_was(self):
        # Line-for-line, which is the property the line numbers depend on — not
        # byte-for-byte: a trailing newline is dropped by the split and rejoin,
        # so "unchanged" would be true only of inputs that happen to lack one.
        text = "a\nb\nc\n"
        self.assertEqual(visible_prose(text).splitlines(), text.splitlines())


if __name__ == "__main__":
    unittest.main()
