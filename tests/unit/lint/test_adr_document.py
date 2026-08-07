#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/lint/adr_document.py`.

`test_check_adr_immutability.py` drives the whole gate through throwaway git
repositories, which is the right shape for the *rule* and the wrong shape for
the parsing underneath it: every case costs three commits, and the cases that
matter here are one-line variations on a header. This module owns the hardest
part of the gate — deciding what a document declares — so it is tested directly,
at the level where a variation is one string.

Four of the gate's silent passes were failures of exactly this parsing: a
`**Supersedes:**` inside a fence, a clause boundary that swallowed the next
bullet, an unreadable `**Status:**` read as "not Accepted", and an unclosed
fence that blanked the frozen sections out of existence.
"""
from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "tools" / "lint"))

from adr_markdown import CannotAnswer, visible_prose  # noqa: E402
from adr_document import (  # noqa: E402
    frozen_spans,
    names_as_superseded,
    sections_of,
    status_of,
    touches,
)

ACCEPTED = """# ADR-0005: A decision

- **Status:** Accepted

## Context

Why.

## Decision

The decision.

## Consequences

- What follows.
"""


class OutsideFences(unittest.TestCase):
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
        self.assertEqual(visible_prose("keep <!-- drop --> this").strip(), "keep   this")

    def test_an_unclosed_comment_is_refused_rather_than_swallowing_the_file(self):
        with self.assertRaises(CannotAnswer) as refused:
            visible_prose("a\n<!-- never closed\nb\n", "adr-0005-a.md")
        self.assertIn("never closed", str(refused.exception))
        self.assertIn("adr-0005-a.md", str(refused.exception))

    # A comment inside a fence is part of the example, not a comment.
    def test_a_comment_inside_a_fence_does_not_leak_out_of_it(self):
        text = "a\n```\n<!-- unclosed inside the example\n```\nb\n"
        self.assertEqual(visible_prose(text).splitlines(), ["a", "", "", "", "b"])


class StatusOf(unittest.TestCase):
    def test_the_house_form(self):
        self.assertEqual(status_of(ACCEPTED, "p"), "Accepted")

    # The two the tree uses today are `- **Status:** Accepted` (eleven ADRs) and
    # the unbulleted form in ADR-0011; the other two are spacing and punctuation
    # markdown allows and a future ADR could arrive with.
    def test_the_status_is_read_however_it_is_bulleted_and_spaced(self):
        for line in (
            "- **Status:** Accepted",
            "**Status:** Accepted",
            "* **Status**: Accepted",
            "-   **Status:**   Accepted",
        ):
            with self.subTest(line=line):
                self.assertEqual(status_of(f"# T\n\n{line}\n", "p"), "Accepted")

    def test_only_the_first_word_is_the_status(self):
        # `- **Status:** Superseded by ADR-0012` is the natural way to write it,
        # and the gate compares the result against "superseded".
        self.assertEqual(
            status_of("# T\n\n- **Status:** Superseded by ADR-0012\n", "p"), "Superseded"
        )

    # The guard `frozen_spans` has had for repeated headings since round six,
    # finally given to Status: read by its first match, so refuse a second.
    # Both lines here are *visible* — a hidden one is blanked before this runs,
    # which is `VisibleProse`'s subject, and was this test's original cover.
    def test_a_repeated_status_is_refused(self):
        doubled = ACCEPTED.replace(
            "- **Status:** Accepted",
            "- **Status:** Accepted\n- **Status:** Superseded",
        )
        with self.assertRaises(CannotAnswer) as refused:
            status_of(doubled, "p")
        self.assertIn("2 `**Status:**` lines", str(refused.exception))
        self.assertIn("Superseded", str(refused.exception))

    # A four-space indent is a code block, and not one `visible_prose` can blank
    # — in a list the same indent is continuation, and the nested
    # `**Supersedes:**` form depends on that. So the field is anchored instead.
    def test_an_indented_status_is_not_the_status(self):
        with self.assertRaises(CannotAnswer):
            status_of("# T\n\n    **Status:** Proposed\n", "p")

    def test_a_status_inside_a_fence_is_an_example_not_a_status(self):
        with self.assertRaises(CannotAnswer) as refused:
            status_of("# T\n\n```\n- **Status:** Accepted\n```\n", "p")
        self.assertIn("no `**Status:**` line", str(refused.exception))

    # A one-character header edit would otherwise disable the gate for that
    # file, permanently and silently.
    def test_an_unreadable_status_is_refused_rather_than_read_as_not_accepted(self):
        with self.assertRaises(CannotAnswer) as refused:
            status_of("# T\n\n- Status: Accepted\n", "adr-0005-a-decision.md")
        self.assertIn("Status", str(refused.exception))
        self.assertIn("adr-0005-a-decision.md", str(refused.exception))


class SectionsOf(unittest.TestCase):
    def test_each_heading_owns_the_lines_up_to_the_next_one(self):
        spans = sections_of(ACCEPTED)
        self.assertEqual(spans["Context"], (5, 8))
        self.assertEqual(spans["Decision"], (9, 12))

    def test_the_last_section_runs_to_the_end_of_the_file(self):
        self.assertEqual(sections_of(ACCEPTED)["Consequences"][1], 15)

    def test_a_heading_inside_a_fence_does_not_open_a_section(self):
        self.assertNotIn("Fenced", sections_of("# T\n\n```\n## Fenced\n```\n"))

    def test_a_repeated_heading_keeps_the_first_span_and_orphans_the_second(self):
        # Both halves stated, because the second is a hole rather than a
        # decision: everything under the *second* `## Decision` belongs to no
        # span at all, so it would be editable while the file reads as having a
        # guarded decision. `sections_of` stays lenient because it also serves
        # the pre-image; `frozen_spans` is where that becomes a fault.
        spans = sections_of("## Decision\n\na\n\n## Decision\n\nb\n")
        self.assertEqual(spans["Decision"], (1, 4))
        self.assertFalse(touches(spans, "Decision", {5, 6, 7}))


class FrozenSpans(unittest.TestCase):
    def test_both_frozen_sections_come_back_with_their_spans(self):
        self.assertEqual(
            frozen_spans(ACCEPTED, "p"), {"Decision": (9, 12), "Consequences": (13, 15)}
        )

    # The hole `sections_of` leaves open, closed here: with two `## Decision`
    # headings only the first owns a span, so the second is unguarded.
    def test_a_repeated_frozen_heading_is_refused(self):
        doubled = ACCEPTED + "\n## Decision\n\nA second one.\n"
        with self.assertRaises(CannotAnswer) as refused:
            frozen_spans(doubled, "p")
        self.assertIn("more than once", str(refused.exception))
        self.assertIn("Decision", str(refused.exception))

    def test_a_missing_section_is_refused_and_named(self):
        with self.assertRaises(CannotAnswer) as refused:
            frozen_spans(ACCEPTED.replace("## Decision", "## The Decision"), "p")
        self.assertIn("Decision", str(refused.exception))

    def test_both_missing_are_named(self):
        with self.assertRaises(CannotAnswer) as refused:
            frozen_spans("# T\n\n- **Status:** Accepted\n", "p")
        self.assertIn("Decision", str(refused.exception))
        self.assertIn("Consequences", str(refused.exception))


class Touches(unittest.TestCase):
    SPANS = {"Decision": (10, 14)}

    def test_the_span_is_inclusive_at_both_ends(self):
        self.assertTrue(touches(self.SPANS, "Decision", {10}))
        self.assertTrue(touches(self.SPANS, "Decision", {14}))

    def test_the_lines_either_side_are_outside_it(self):
        self.assertFalse(touches(self.SPANS, "Decision", {9}))
        self.assertFalse(touches(self.SPANS, "Decision", {15}))

    def test_a_change_touching_nothing_touches_no_section(self):
        self.assertFalse(touches(self.SPANS, "Decision", set()))

    def test_an_absent_section_is_never_touched(self):
        self.assertFalse(touches(self.SPANS, "Consequences", {10}))


if __name__ == "__main__":
    unittest.main()
