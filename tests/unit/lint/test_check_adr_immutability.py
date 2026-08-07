#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The rule: what is frozen, and what excuses an edit.

The three companions: `test_adr_gate_faults.py` holds everything that must
exit 2, `test_adr_gate_ranges.py` holds what a *range* changed, and
`adr_fixture.py` holds the throwaway repository all three drive.

Split twice. At 457 lines it became the rule and the range; at 326 the rule
and the faults — each time because AGENTS.md §2's limit is about
responsibilities, and `guard-limits` does not cover `tests/` to say so.
"""
from __future__ import annotations

import unittest

from adr_fixture import ADR_DIR, AdrGateTest, git, successor


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

    # The acceptance criterion "zero for a change to Status" had no test of its
    # own: every Status case also edited the Decision, so none of them showed
    # that the Status line alone is free. Status *must* be free — it is how a
    # supersession is recorded at all.
    def test_changing_the_status_alone_passes(self):
        self.substitute("- **Status:** Accepted", "- **Status:** Superseded")
        self.repo.commit("mark it superseded and nothing else")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 0, outcome.stdout + outcome.stderr)

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
                self.reset()
                outcome = self.demote_and_rewrite(status)
                self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
                # Named, not merely non-zero: any exit-1 reason would satisfy a
                # bare returncode check, including one from an unrelated record.
                self.assertIn("ADR-0005", outcome.stderr)
                self.assertIn("Decision", outcome.stderr)

    # The two-commit version of the same escape, which the fallback for an
    # unparseable pre-image reopened. Step one mangles the header — outside
    # every frozen span, so it used to pass; step two demotes and rewrites, and
    # the fallback read `Proposed` from the post-image. Step one is the fault
    # now, which is what makes step two unreachable.
    def test_mangling_the_status_header_is_refused_rather_than_tolerated(self):
        self.substitute("- **Status:** Accepted", "- Status: Accepted")
        self.repo.commit("tidy the header")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("Status", outcome.stderr)

    # And `Superseded` is the one that does. The branch this pins is the
    # transition test in `breaches_in` — `before == "accepted" and after ==
    # "superseded"`; delete it and this test fails. (An earlier version of this
    # comment named `superseded_by_the_same_change`, which is neither the
    # mechanism nor, since round five, a function that exists.)
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


if __name__ == "__main__":
    unittest.main()
