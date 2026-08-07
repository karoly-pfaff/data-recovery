#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The rule: what is frozen, what excuses an edit, and what is a fault.

What a *range* changed is `test_adr_gate_ranges.py`; the fixtures both use are
`adr_fixture.py`. Split when the single file reached 457 lines — 83% over the
limit the gate itself had just been split for.
"""
from __future__ import annotations

import unittest

from adr_fixture import AdrGateTest, successor


class FrozenSections(AdrGateTest):
    def test_editing_the_decision_of_an_accepted_adr_fails(self):
        self.edit("Decision", "Something else entirely.")
        self.repo.commit("rewrite the decision")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1)
        self.assertIn("ADR-0005", outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    def test_editing_the_consequences_of_an_accepted_adr_fails(self):
        self.edit("Consequences", "- Something else follows.")
        self.repo.commit("rewrite the consequences")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1)
        self.assertIn("Consequences", outcome.stderr)

    # `@@ -8 +7,0 @@` gains no line on the new side, so `range(start, start+0)`
    # is empty and the edit records as untouched. Deleting a consequence you no
    # longer like is at least as much a breach as adding one, and quieter.
    def test_deleting_a_line_from_a_frozen_section_fails(self):
        self.substitute("- What follows from it.\n", "")
        self.repo.commit("delete a consequence")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Consequences", outcome.stderr)

    # A `## Decision` inside a fence must not relocate the frozen span.
    def test_a_heading_inside_a_code_fence_does_not_move_the_frozen_sections(self):
        self.repo.write(
            "adr-0005-a-decision.md",
            self.repo.read()
            + "\nAn example of the shape:\n\n```markdown\n## Decision\n\nnot real\n```\n",
        )
        self.repo.commit("add a fenced example")
        self.edit("Decision", "Rewritten while a fence claims otherwise.")
        self.repo.commit("rewrite the real decision")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)


class NotFrozen(AdrGateTest):
    def test_editing_the_context_passes(self):
        self.edit("Context", "A fuller account of why.")
        self.repo.commit("expand the context")
        self.assertEqual(self.gate().returncode, 0)

    def test_changing_the_date_passes(self):
        self.substitute("- **Date:** 2026-01-01", "- **Date:** 2026-02-02")
        self.repo.commit("correct the date")
        self.assertEqual(self.gate().returncode, 0)

    def test_a_proposed_adr_may_be_edited_freely(self):
        self.substitute("- **Status:** Accepted", "- **Status:** Proposed")
        self.repo.commit("back to proposed")
        self.edit("Decision", "Rewritten while still proposed.")
        self.repo.commit("rewrite while proposed")
        self.assertEqual(self.gate().returncode, 0)


class WhichSideOwnsTheStatus(AdrGateTest):
    """The pre-image decides whether the record was frozen.

    Asked of the post-image — as it was — one commit that demotes the Status and
    rewrites the Decision passes with exit 0. That is a general-purpose escape
    hatch reachable by anyone editing the file they are already editing, which
    is exactly the `--allow` flag this gate was designed not to have.
    """

    def demote_and_rewrite(self, status: str):
        self.substitute("- **Status:** Accepted", f"- **Status:** {status}")
        self.edit("Decision", "Rewritten in the same breath.")
        self.repo.commit(f"demote to {status} and rewrite")
        return self.gate()

    def test_demoting_to_proposed_in_the_same_change_does_not_unfreeze_it(self):
        outcome = self.demote_and_rewrite("Proposed")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    def test_no_other_status_unfreezes_it_either(self):
        for status in ("Draft", "Rejected", "Deprecated"):
            with self.subTest(status=status):
                self.setUp()
                self.assertEqual(self.demote_and_rewrite(status).returncode, 1)

    # And `Superseded` is the one that does — which is what makes the escape at
    # the bottom of `superseded_by_the_same_change` load-bearing rather than
    # dead. Replace that line with `return False` and this test fails.
    def test_superseded_is_the_one_status_that_does(self):
        outcome = self.demote_and_rewrite("Superseded")
        self.assertEqual(outcome.returncode, 0, outcome.stdout + outcome.stderr)


class TheEscapes(AdrGateTest):
    def test_a_new_adr_naming_it_superseded_permits_the_edit(self):
        self.edit("Decision", "Superseded text, left as the record.")
        self.repo.write(
            "adr-0006-the-successor.md",
            successor("- **Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n"),
        )
        self.repo.commit("supersede ADR-0005")
        self.assertEqual(self.gate().returncode, 0)

    # The loose reading this gate refuses: a new ADR is not a blank cheque.
    def test_a_new_adr_naming_a_different_adr_does_not_permit_the_edit(self):
        self.edit("Decision", "Quietly rewritten.")
        self.repo.write(
            "adr-0006-the-successor.md",
            successor("- **Supersedes:** [ADR-0009](adr-0009-elsewhere.md)\n"),
        )
        self.repo.commit("add an unrelated ADR while rewriting 0005")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1)
        self.assertIn("ADR-0005", outcome.stderr)

    # Prose is not a declaration. ADR-0012 says "an Accepted ADR is superseded
    # by a new record, not edited" two sentences from "ADR-0005", and reading
    # that as a claim excused the very edit this gate refuses.
    def test_prose_mentioning_supersession_does_not_permit_the_edit(self):
        self.edit("Decision", "Quietly rewritten.")
        self.repo.write(
            "adr-0006-the-successor.md",
            successor(
                "\n## Context\n\n"
                "An Accepted ADR is superseded by a new record rather than edited,\n"
                "which is why ADR-0005 matters here.\n"
            ),
        )
        self.repo.commit("add an ADR that merely discusses supersession")
        self.assertEqual(self.gate().returncode, 1)

    # An example is not a declaration either. An ADR about the ADR process
    # would illustrate the header — and excuse whatever it named.
    def test_a_supersedes_header_inside_a_code_fence_does_not_permit_the_edit(self):
        self.edit("Decision", "Quietly rewritten.")
        self.repo.write(
            "adr-0006-the-successor.md",
            successor(
                "\n## Context\n\nAn ADR declares what it replaces like this:\n\n"
                "```markdown\n- **Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n```\n"
            ),
        )
        self.repo.commit("add an ADR that illustrates the header")
        self.assertEqual(self.gate().returncode, 1)

    # The natural multi-ADR form. Stopping the clause at any bullet made this a
    # false breach — the gate refusing the change it exists to encourage.
    def test_a_supersedes_list_naming_several_adrs_permits_the_edit(self):
        self.edit("Decision", "Superseded text, left as the record.")
        self.repo.write(
            "adr-0006-the-successor.md",
            successor(
                "- **Supersedes:**\n"
                "  - [ADR-0004](adr-0004-earlier.md)\n"
                "  - [ADR-0005](adr-0005-a-decision.md)\n"
            ),
        )
        self.repo.commit("supersede two ADRs at once")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 0, outcome.stdout + outcome.stderr)


class AddingIsNotEditing(AdrGateTest):
    def test_adding_a_new_adr_is_not_an_edit(self):
        self.repo.write("adr-0006-brand-new.md", successor())
        self.repo.commit("add an ADR")
        self.assertEqual(self.gate().returncode, 0)

    # Everything else here is strict about the post-image and forgiving about
    # the pre-image, which only works if a malformed record can never enter.
    # Added files were parsed by nothing, so one landing with an unclosed fence
    # made every later range over it exit 2 — including the commit that would
    # have repaired it. There was no green state at all.
    def test_an_accepted_adr_may_not_land_with_an_unclosed_fence(self):
        self.repo.write(
            "adr-0006-broken.md", successor() + "\n```markdown\nnever closed\n"
        )
        self.repo.commit("land a malformed ADR")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("fence", outcome.stderr)

    def test_an_accepted_adr_may_not_land_without_its_frozen_sections(self):
        self.repo.write(
            "adr-0006-thin.md",
            "# ADR-0006\n\n- **Status:** Accepted\n\n## Context\n\nOnly context.\n",
        )
        self.repo.commit("land an incomplete ADR")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    # A Proposed ADR is a draft and may land in any shape; it is not frozen.
    def test_a_proposed_adr_may_land_incomplete(self):
        self.repo.write(
            "adr-0006-draft.md",
            "# ADR-0006\n\n- **Status:** Proposed\n\n## Context\n\nStill thinking.\n",
        )
        self.repo.commit("land a draft")
        self.assertEqual(self.gate().returncode, 0)


class UnreadableIsAFault(AdrGateTest):
    """Exit 2, never a pass — the milestone's subject, self-applied."""

    # A one-character header edit would otherwise disable the gate for that
    # file, permanently and silently.
    def test_an_unreadable_status_is_a_fault_not_a_pass(self):
        self.substitute("- **Status:** Accepted", "- Status: Accepted")
        self.repo.commit("reformat the status line")
        self.edit("Decision", "Rewritten behind an unreadable header.")
        self.repo.commit("rewrite the decision")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("Status", outcome.stderr)

    # An unclosed fence blanked the rest of the file, so no Decision was found
    # and the Decision could be rewritten freely — the silent pass the Status
    # fault removed, reintroduced by the fix for fenced examples.
    def test_an_unclosed_code_fence_is_a_fault_not_a_pass(self):
        self.repo.write(
            "adr-0005-a-decision.md", self.repo.read() + "\n```markdown\nnever closed\n"
        )
        self.repo.commit("leave a fence open")
        self.edit("Decision", "Rewritten behind an open fence.")
        self.repo.commit("rewrite the decision")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("fence", outcome.stderr)

    # Renaming the heading would otherwise unfreeze the section silently: an
    # absent section touches nothing.
    def test_an_accepted_adr_missing_a_frozen_section_is_a_fault(self):
        self.substitute("## Decision", "## The Decision")
        self.repo.commit("rename the heading")
        self.substitute("The decision, as accepted.", "Rewritten.")
        self.repo.commit("rewrite behind the renamed heading")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    # The same hole from the other side: with two `## Decision` headings only
    # the first owns a span, so everything under the second is unguarded.
    def test_a_repeated_frozen_heading_is_a_fault(self):
        self.repo.write(
            "adr-0005-a-decision.md",
            self.repo.read() + "\n## Decision\n\nA second one.\n",
        )
        self.repo.commit("add a second decision heading")
        self.substitute("A second one.", "Rewritten under the duplicate.")
        self.repo.commit("rewrite under the duplicate")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("more than once", outcome.stderr)


if __name__ == "__main__":
    unittest.main()
