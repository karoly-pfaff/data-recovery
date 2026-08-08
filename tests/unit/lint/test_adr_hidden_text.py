#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Text a reader never sees speaks for nothing — at the gate's own level.

`test_adr_document.py::VisibleProse` covers the blanking over strings; these
drive the whole gate, because the defect was never about markdown. Three shapes
reached a record permanently unfrozen or an edit excused, all rendering exactly
like a well-formed ADR, and the cover — an HTML comment — is how every file in
the tree opens.
"""
from __future__ import annotations

import unittest

from adr_fixture import AdrGateTest


class TextNobodySees(AdrGateTest):
    """An HTML comment is invisible to a reader and was prose to this gate.

    Every ADR opens with one, so the cover was idiomatic in every file. Three
    shapes were reproduced before it was closed, all rendering identically to a
    well-formed record: a hidden `**Status:**`, hidden frozen headings, and a
    hidden `**Supersedes:**` excusing an edit to a record already in the tree.
    """

    HIDDEN = "<!--\n{}\n-->\n\n"

    def test_a_hidden_status_does_not_speak_for_the_record(self):
        # The visible header still says Accepted, so the record is frozen and
        # the rewrite is a breach — not a fault, because there is no ambiguity
        # once the comment is blanked.
        self.repo.write(
            "adr-0005-a-decision.md",
            self.HIDDEN.format("**Status:** Proposed") + self.repo.read(),
        )
        self.substitute("The decision, as accepted.", "Reversed entirely.")
        self.repo.commit("housekeeping")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    def test_a_record_cannot_be_born_unfrozen_behind_a_comment(self):
        self.repo.write(
            "adr-0013-something.md",
            self.HIDDEN.format("**Status:** Proposed")
            + "# ADR-0013\n\n- **Status:**&nbsp;Accepted\n\n"
            "## Decision\n\nThe decision.\n\n## Consequences\n\n- Follows.\n",
        )
        self.repo.commit("land a new ADR")
        outcome = self.gate()
        # The hidden line is gone and `&nbsp;` defeats the visible one, so the
        # record has no readable Status at all — which is already a fault.
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("Status", outcome.stderr)

    def test_hidden_headings_do_not_move_the_frozen_spans(self):
        self.repo.write(
            "adr-0014-another.md",
            self.HIDDEN.format("## Decision\n## Consequences")
            + "# ADR-0014\n\n- **Status:** Accepted\n\n"
            "## Decision ##\n\nThe decision.\n\n## Consequences ##\n\n- Follows.\n",
        )
        self.repo.commit("land it")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    def test_a_hidden_supersedes_does_not_excuse_an_edit(self):
        self.substitute("The decision, as accepted.", "Quietly rewritten.")
        self.repo.write(
            "adr-0013-the-successor.md",
            self.HIDDEN.format("- **Supersedes:** ADR-0005")
            + "# ADR-0013\n\n- **Status:** Accepted\n\n"
            "## Decision\n\nNew.\n\n## Consequences\n\n- Follows.\n",
        )
        self.repo.commit("a successor that declares nothing a reader can see")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("ADR-0005", outcome.stderr)

    def test_an_unclosed_comment_is_a_fault(self):
        # At the end, where nothing closes it. Opening one *above* the SPDX
        # header would be closed by that header's own `-->`, which is what a
        # renderer does too — the first version of this test asserted a fault
        # that correct behaviour had already prevented.
        self.repo.write(
            "adr-0005-a-decision.md", self.repo.read() + "\n<!-- never closed\n"
        )
        self.repo.commit("open a comment and leave it")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("never closed", outcome.stderr)


if __name__ == "__main__":
    unittest.main()
