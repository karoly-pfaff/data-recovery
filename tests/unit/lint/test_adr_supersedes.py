#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a record declares it supersedes, and which record a path names.

Split from `test_adr_document.py`, which covers fences, headings and Status.
The `**Supersedes:**` clause is the gate's only escape, so where it starts and
stops is worth its own module: three of this gate's silent passes were a
too-generous reading of it — prose, a fenced example, and a fixed-width window
that swallowed the next field.
"""
from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "tools" / "lint"))

from adr_document import (  # noqa: E402
    CannotAnswer,
    adr_number,
    names_as_superseded,
)


class NamesAsSuperseded(unittest.TestCase):
    HEADER = "# ADR-0006\n\n- **Status:** Accepted\n"

    def test_the_header_field_declares_it(self):
        text = f"{self.HEADER}- **Supersedes:** [ADR-0005](adr-0005-a.md)\n"
        self.assertTrue(names_as_superseded(text, "0005"))

    def test_the_field_is_recognised_however_it_is_punctuated(self):
        for field in ("**Supersedes:**", "**Supersedes**", "**supersedes:**"):
            with self.subTest(field=field):
                self.assertTrue(
                    names_as_superseded(f"{self.HEADER}- {field} ADR-0005\n", "0005")
                )

    def test_a_nested_list_is_the_natural_multi_adr_form(self):
        text = (
            f"{self.HEADER}- **Supersedes:**\n"
            "  - [ADR-0004](adr-0004-a.md)\n"
            "  - [ADR-0005](adr-0005-b.md)\n\n"
            "## Decision\n"
        )
        self.assertTrue(names_as_superseded(text, "0004"))
        self.assertTrue(names_as_superseded(text, "0005"))

    # The clause stops at the next top-level bullet. A fixed character window
    # swallowed it, which excused an edit to whatever the *next* field named.
    def test_the_next_field_is_not_part_of_the_clause(self):
        text = (
            f"{self.HEADER}- **Supersedes:** [ADR-0004](adr-0004-a.md)\n"
            "- **Related:** [ADR-0005](adr-0005-b.md)\n"
        )
        self.assertTrue(names_as_superseded(text, "0004"))
        self.assertFalse(names_as_superseded(text, "0005"))

    def test_a_blank_line_ends_the_clause(self):
        text = f"{self.HEADER}- **Supersedes:** ADR-0004\n\nADR-0005 is discussed below.\n"
        self.assertFalse(names_as_superseded(text, "0005"))

    # Prose is not a declaration. ADR-0012's Context says "an Accepted ADR is
    # superseded by a new record, not edited" two sentences from "ADR-0005".
    def test_prose_about_supersession_declares_nothing(self):
        text = (
            f"{self.HEADER}\n## Context\n\n"
            "An Accepted ADR is superseded by a new record rather than edited,\n"
            "which is why ADR-0005 matters here.\n"
        )
        self.assertFalse(names_as_superseded(text, "0005"))

    # An example is not a declaration either — an ADR about the ADR process
    # would illustrate the header, and excuse whatever it named.
    def test_a_fenced_example_declares_nothing(self):
        text = (
            f"{self.HEADER}\n## Context\n\nLike this:\n\n"
            "```markdown\n- **Supersedes:** [ADR-0005](adr-0005-a.md)\n```\n"
        )
        self.assertFalse(names_as_superseded(text, "0005"))

    def test_declaring_one_adr_does_not_declare_its_neighbour(self):
        text = f"{self.HEADER}- **Supersedes:** [ADR-0005](adr-0005-a.md)\n"
        self.assertFalse(names_as_superseded(text, "0006"))


class AdrNumber(unittest.TestCase):
    def test_the_number_comes_from_the_path(self):
        self.assertEqual(adr_number("docs/architecture/adr/adr-0005-a-decision.md"), "0005")

    def test_a_path_outside_the_adr_directory_is_refused(self):
        for path in (
            "docs/adr-0005-a-decision.md",
            "docs/architecture/adr/README.md",
            "docs/architecture/adr/adr-5-short.md",
        ):
            with self.subTest(path=path):
                with self.assertRaises(CannotAnswer):
                    adr_number(path)


if __name__ == "__main__":
    unittest.main()
