#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The two ways an edit to a frozen section may be excused, and their limits.

Split from `test_check_adr_immutability.py`, which asks what is frozen. The gate
has exactly two justifications — a new record declaring `**Supersedes:**`, and
the record's own Status making that transition — and most of this story's
defects were in how loosely they were read, or in how easily the evidence for
one could be taken back afterwards.
"""
from __future__ import annotations

import unittest

from adr_fixture import AdrGateTest, successor


class TheEscapes(AdrGateTest):
    def test_a_new_adr_naming_it_superseded_permits_the_edit(self):
        self.edit("Decision", "Superseded text, left as the record.")
        self.repo.write(
            "adr-0006-the-successor.md",
            successor("- **Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n"),
        )
        self.repo.commit("supersede ADR-0005")
        self.assertEqual(self.gate().returncode, 0)

    # The status of the declaring record is the whole weight of the escape. A
    # two-line `Proposed` draft used to unlock the rewrite — and since a draft
    # that was never accepted may be freely withdrawn, the next change deleted
    # it and left no trace at all. Two green pull requests.
    def test_a_draft_successor_does_not_permit_the_edit(self):
        self.edit("Decision", "Quietly rewritten.")
        self.repo.write(
            "adr-0006-the-successor.md",
            "# ADR-0006\n\n- **Status:** Proposed\n"
            "- **Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n",
        )
        self.repo.commit("rewrite ADR-0005 under a draft successor")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("ADR-0005", outcome.stderr)

    # Nor may a record excuse itself under a second file carrying its number.
    def test_a_second_file_with_the_same_number_is_not_a_successor(self):
        self.edit("Decision", "Quietly rewritten.")
        self.repo.write(
            "adr-0005-a-second-file.md",
            successor("- **Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n"),
        )
        self.repo.commit("rewrite ADR-0005 and add another ADR-0005")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("ADR-0005", outcome.stderr)

    # The declaration is what the escape was bought with, and it lived in the
    # header — outside both frozen sections. So a successor could be admitted,
    # spend its excuse, and then have the clause tidied away in a change that
    # touched no frozen line: the rewritten Decision left with nothing pointing
    # at it. An escape's evidence must be as durable as the thing it excuses.
    def test_the_declaration_may_not_be_withdrawn_afterwards(self):
        clause = "- **Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n"
        self.edit("Decision", "Rewritten under a successor.")
        self.repo.write("adr-0006-the-successor.md", successor(clause))
        self.repo.commit("supersede ADR-0005 and rewrite it")
        self.assertEqual(self.gate().returncode, 0, "the designed escape")

        self.repo.write(
            "adr-0006-the-successor.md",
            self.repo.read("adr-0006-the-successor.md").replace(clause, ""),
        )
        self.repo.commit("tidy the successor's header")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("no longer declares", outcome.stderr)
        self.assertIn("ADR-0005", outcome.stderr)

    # Adding one stays free.
    def test_declaring_a_further_supersession_is_not_a_withdrawal(self):
        self.repo.write(
            "adr-0006-the-successor.md",
            successor("- **Supersedes:** [ADR-0004](adr-0004-earlier.md)\n"),
        )
        self.repo.commit("add a successor")
        self.repo.write(
            "adr-0006-the-successor.md",
            successor(
                "- **Supersedes:**\n"
                "  - [ADR-0004](adr-0004-earlier.md)\n"
                "  - [ADR-0003](adr-0003-earlier-still.md)\n"
            ),
        )
        self.repo.commit("it supersedes another one too")
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


class TheLimitsOfTheDeclarationRule(AdrGateTest):
    """Two behaviours that were emergent, so they are asserted deliberately.

    A declaration may grow but not shrink, which is what stops an excuse being
    taken back after it is spent — and which also refuses a correction. The gate
    cannot tell the two apart without reading further back than the range it was
    handed, so the refusal is the design, not an oversight, and it is pinned
    here rather than left to be rediscovered as a bug.
    """

    def add_successor(self, clause: str) -> None:
        self.repo.write("adr-0006-the-successor.md", successor(clause))
        self.repo.commit("add a successor")

    def rewrite_clause(self, clause: str, message: str) -> None:
        self.repo.write("adr-0006-the-successor.md", successor(clause))
        self.repo.commit(message)

    def test_correcting_a_mistaken_number_is_refused(self):
        self.add_successor("- **Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n")
        self.rewrite_clause(
            "- **Supersedes:** [ADR-0015](adr-0015-what-was-meant.md)\n",
            "that was a typo",
        )
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("no longer declares", outcome.stderr)

    # …and the way out, which the documentation now names: get it right before
    # the record lands. An added record's declaration is compared against
    # nothing, so the whole first change is free.
    def test_getting_it_right_before_the_record_lands_is_free(self):
        self.repo.write(
            "adr-0006-the-successor.md",
            successor("- **Supersedes:** [ADR-0015](adr-0015-what-was-meant.md)\n"),
        )
        self.repo.commit("add a successor, declared correctly the first time")
        self.assertEqual(self.gate().returncode, 0)

    # Reformatting the same numbers is not a withdrawal.
    def test_reformatting_the_clause_is_not_a_withdrawal(self):
        self.add_successor(
            "- **Supersedes:**\n"
            "  - [ADR-0005](adr-0005-a-decision.md)\n"
            "  - [ADR-0004](adr-0004-earlier.md)\n"
        )
        self.rewrite_clause(
            "- **Supersedes:** [ADR-0005](adr-0005-a-decision.md), "
            "[ADR-0004](adr-0004-earlier.md)\n",
            "one line reads better",
        )
        self.assertEqual(self.gate().returncode, 0)


class WhatCountsAsDeclaring(AdrGateTest):
    """The boundary is the **bold field**, not its position on the page.

    `quality-gates.md` states the choice: the gate does not insist the field sit
    in the header, because refusing a correctly-declared supersession over its
    position would teach people to route around the gate. The cost is that
    bolding the field name mid-prose declares — asserted here so the trade is
    visible rather than discovered.
    """

    def test_prose_that_bolds_the_field_name_does_declare(self):
        self.edit("Decision", "Rewritten.")
        self.repo.write(
            "adr-0006-the-successor.md",
            successor("")
            + "\n## Notes\n\n**Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n",
        )
        self.repo.commit("declare it further down the page")
        self.assertEqual(self.gate().returncode, 0)

    def test_prose_that_does_not_bold_it_does_not(self):
        self.edit("Decision", "Rewritten.")
        self.repo.write(
            "adr-0006-the-successor.md",
            successor("")
            + "\n## Notes\n\nThis supersedes [ADR-0005](adr-0005-a-decision.md).\n",
        )
        self.repo.commit("merely mention it")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("ADR-0005", outcome.stderr)


if __name__ == "__main__":
    unittest.main()
