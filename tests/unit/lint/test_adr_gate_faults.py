#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Everything the gate must refuse to answer: exit 2, never a pass.

Split out of `test_check_adr_immutability.py`, which holds the rule itself.
These are the cases where the gate cannot tell whether a record is frozen —
and each one was a silent pass before it was a fault.
"""
from __future__ import annotations

import unittest

import os
import subprocess
import sys

from adr_fixture import GATE, ADR_DIR, AdrGateTest, git, successor


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
        self.repo.write("adr-0006-draft.md", self.DRAFT.format("Proposed"))
        self.repo.commit("land a draft")
        self.assertEqual(self.gate().returncode, 0)

    DRAFT = "# ADR-0006\n\n- **Status:** {}\n\n## Context\n\nStill thinking.\n"

    # …and promoting that draft is the other arrival. Checking *additions* let
    # an incomplete record in through two commits instead of one, after which
    # every edit exited 2 and the repair exited 1 — the dead end the landing
    # check exists to prevent, reached the long way round.
    def test_promoting_an_incomplete_draft_to_accepted_is_refused(self):
        self.repo.write("adr-0006-draft.md", self.DRAFT.format("Proposed"))
        self.repo.commit("land a draft")
        self.assertEqual(self.gate().returncode, 0, "the draft itself is allowed")

        self.repo.write("adr-0006-draft.md", self.DRAFT.format("Accepted"))
        self.repo.commit("promote it while still incomplete")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    # A rename *into* the convention is an arrival too, and git reports it as a
    # rename rather than an addition when the file was already in the directory.
    def test_renaming_a_file_into_the_convention_is_an_arrival(self):
        self.repo.write("README.md", self.DRAFT.format("Accepted").replace("ADR-0006", "Index"))
        self.repo.commit("an index file in the ADR directory")
        git(self.repo.root, "mv", f"{ADR_DIR}/README.md", f"{ADR_DIR}/adr-0013-arrived.md")
        self.repo.commit("rename it into the convention")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)


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


class ASecondStatusLine(AdrGateTest):
    """Reading the *first* `**Status:**` let a hidden one decide the answer.

    An ADR opens with an HTML comment, and `outside_fences` blanks code fences
    only — so the cover is idiomatic in every file in the tree. Both shapes were
    reproduced before the guard was written: one commit rewriting both frozen
    sections of an Accepted record, and a new record born permanently unfrozen
    while rendering as `Accepted`.
    """

    HIDDEN = "<!--\n**Status:** {}\n-->\n"

    def test_a_hidden_status_does_not_unfreeze_an_existing_record(self):
        self.repo.write(
            "adr-0005-a-decision.md",
            self.HIDDEN.format("Superseded") + self.repo.read(),
        )
        self.substitute("The decision, as accepted.", "Reversed entirely.")
        self.repo.commit("housekeeping")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("`**Status:**` lines", outcome.stderr)

    def test_a_record_cannot_be_born_with_two_statuses(self):
        self.repo.write(
            "adr-0006-born-unfrozen.md",
            self.HIDDEN.format("Proposed")
            + "# ADR-0006\n\n- **Status:** Accepted\n\n"
            "## Decision\n\nNew.\n\n## Consequences\n\n- New.\n",
        )
        self.repo.commit("land a new ADR")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("`**Status:**` lines", outcome.stderr)


class TheGatesOwnRoot(AdrGateTest):
    """`git diff -- <pathspec>` is silent and exit 0 when nothing matches.

    story-0704 built this refusal for every walking gate; this one is exempt
    from that meta-test because it reads a range rather than a tree, and the
    exemption left the hole. Gate 14 runs on pull requests only, so the change
    that relocates the ADR directory reaches `main` without ever meeting the
    gate it silences — after which every later run covers an empty set and says
    so in the words of a clean pass.
    """

    # `README.md` lives in that directory and is the one path the tests assert
    # is *not* an ADR, so "the directory holds a file" is not "the gate covers a
    # record". A directory holding only the index covers nothing at all.
    def test_a_directory_holding_only_the_index_is_a_fault(self):
        git(self.repo.root, "rm", "-q", f"{ADR_DIR}/adr-0005-a-decision.md")
        self.repo.write("README.md", "# The ADR index\n")
        self.repo.commit("remove the last ADR, keep the index")
        self.assertEqual(self.gate().returncode, 1, "the removal itself is reported")

        (self.repo.root / "notes.md").write_text("unrelated\n", encoding="utf-8")
        self.repo.commit("an unrelated change, later")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("no ADRs under", outcome.stderr)

    # And the guard must not be skippable by touching a non-ADR file in the
    # directory: what matters is whether this range reported on a *record*, not
    # whether it touched a file.
    def test_touching_a_non_adr_file_does_not_skip_the_guard(self):
        git(self.repo.root, "rm", "-q", f"{ADR_DIR}/adr-0005-a-decision.md")
        self.repo.write("README.md", "# The ADR index\n")
        self.repo.commit("remove the last ADR, keep the index")

        self.repo.write("README.md", "# The ADR index, reworded\n")
        self.repo.commit("edit the index")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("no ADRs under", outcome.stderr)

    def test_a_relocated_adr_directory_is_a_fault_not_a_clean_pass(self):
        (self.repo.root / "docs" / "decisions").mkdir(parents=True)
        git(self.repo.root, "mv", f"{ADR_DIR}/adr-0005-a-decision.md",
            "docs/decisions/adr-0005-a-decision.md")
        self.repo.commit("relocate the ADRs")
        moved = self.gate()
        self.assertEqual(moved.returncode, 1, "the move itself is a removal")

        (self.repo.root / "notes.md").write_text("unrelated\n", encoding="utf-8")
        self.repo.commit("an unrelated change, later")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("empty gate", outcome.stderr)

    # "No ADR changed" is the ordinary case and must stay a pass.
    def test_a_range_touching_no_adr_still_passes_while_the_directory_is_there(self):
        (self.repo.root / "notes.md").write_text("unrelated\n", encoding="utf-8")
        self.repo.commit("an unrelated change")
        self.assertEqual(self.gate().returncode, 0)


class FaultsThatAreNotBreaches(AdrGateTest):
    """Exit 2 means "could not run"; exit 1 means "found a violation".

    `quality-gates.md` draws that line, and story-0704 is the milestone's whole
    argument for it. Anything that escaped as a traceback exited **1** — the
    code reserved for a breach — so a gate that could not read its input
    reported one that was not there.
    """

    def test_a_byte_that_is_not_utf8_is_a_fault_rather_than_a_breach(self):
        # `docs/` is outside `check_encoding.py`'s roots, so nothing else in
        # the tree catches this first.
        self.repo.path("adr-0005-a-decision.md").write_bytes(
            self.repo.read().encode("utf-8").replace(b"Why.", b"Wh\xff.")
        )
        self.repo.commit("a byte that is not UTF-8")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("UTF-8", outcome.stderr)


class WithoutGitAtAll(AdrGateTest):
    """`quality-gates.md` claims "no `git` on the path" is a fault, so pin it.

    Constructible, unlike the non-UTF-8 repository path the story names as its
    one unpinned fix: emptying `PATH` leaves the interpreter running — it was
    launched by absolute path — and every `git` lookup failing.
    """

    def test_it_is_a_fault_rather_than_a_breach(self):
        outcome = subprocess.run(
            [sys.executable, str(GATE), "HEAD~1..HEAD"],
            cwd=self.repo.root, capture_output=True, text=True, check=False,
            encoding="utf-8", env={"PATH": "", "SYSTEMROOT": os.environ.get("SYSTEMROOT", "")},
        )
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("could not run git", outcome.stderr)


if __name__ == "__main__":
    unittest.main()
