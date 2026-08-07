#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/lint/check_adr_immutability.py`.

Over throwaway git repositories rather than this one: the cases have to stay
stable while the real ADRs keep changing. The one exception is the historical
breach, which is asserted against the real `4a4221e` — a gate written to catch a
specific commit is unverified until it has been run against that commit.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
GATE = REPO_ROOT / "tools" / "lint" / "check_adr_immutability.py"

ADR_DIR = "docs/architecture/adr"

ACCEPTED = """<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0005: A decision

- **Status:** Accepted
- **Date:** 2026-01-01

## Context

Why.

## Decision

The decision, as accepted.

## Consequences

- What follows from it.
"""


def git(repo: pathlib.Path, *args: str) -> str:
    done = subprocess.run(
        ["git", *args], cwd=repo, capture_output=True, text=True, check=True, encoding="utf-8"
    )
    return done.stdout


def run_gate(repo: pathlib.Path, diff_range: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(GATE), diff_range],
        cwd=repo,
        capture_output=True,
        text=True,
        check=False,
        encoding="utf-8",
    )


class AdrRepository:
    """A repository with one Accepted ADR committed, ready to be edited."""

    def __init__(self, root: pathlib.Path):
        self.root = root
        git(root, "init", "-q", "-b", "main")
        git(root, "config", "user.email", "gate@test")
        git(root, "config", "user.name", "Gate Test")
        self.write("adr-0005-a-decision.md", ACCEPTED)
        self.commit("the accepted ADR")

    def path(self, name: str) -> pathlib.Path:
        return self.root / ADR_DIR / name

    def write(self, name: str, text: str) -> None:
        target = self.path(name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")

    def commit(self, message: str) -> None:
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", message)


class AdrImmutability(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.repo = AdrRepository(pathlib.Path(self._tmp.name))
        self.addCleanup(self._tmp.cleanup)

    def edit(self, section: str, text: str) -> None:
        current = self.repo.path("adr-0005-a-decision.md").read_text(encoding="utf-8")
        marker = f"## {section}\n"
        head, _, tail = current.partition(marker)
        rest = tail.split("\n##", 1)
        replaced = f"{head}{marker}\n{text}\n" + ("\n##" + rest[1] if len(rest) > 1 else "")
        self.repo.write("adr-0005-a-decision.md", replaced)

    # --- the frozen sections ------------------------------------------------
    def test_editing_the_decision_of_an_accepted_adr_fails(self):
        self.edit("Decision", "Something else entirely.")
        self.repo.commit("rewrite the decision")
        outcome = run_gate(self.repo.root, "HEAD~1..HEAD")
        self.assertEqual(outcome.returncode, 1)
        self.assertIn("ADR-0005", outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    def test_editing_the_consequences_of_an_accepted_adr_fails(self):
        self.edit("Consequences", "- Something else follows.")
        self.repo.commit("rewrite the consequences")
        outcome = run_gate(self.repo.root, "HEAD~1..HEAD")
        self.assertEqual(outcome.returncode, 1)
        self.assertIn("Consequences", outcome.stderr)

    # --- what is not frozen -------------------------------------------------
    def test_editing_the_context_passes(self):
        self.edit("Context", "A fuller account of why.")
        self.repo.commit("expand the context")
        self.assertEqual(run_gate(self.repo.root, "HEAD~1..HEAD").returncode, 0)

    def test_changing_the_status_and_date_passes(self):
        current = self.repo.path("adr-0005-a-decision.md").read_text(encoding="utf-8")
        self.repo.write(
            "adr-0005-a-decision.md",
            current.replace("- **Date:** 2026-01-01", "- **Date:** 2026-02-02"),
        )
        self.repo.commit("correct the date")
        self.assertEqual(run_gate(self.repo.root, "HEAD~1..HEAD").returncode, 0)

    def test_a_proposed_adr_may_be_edited_freely(self):
        current = self.repo.path("adr-0005-a-decision.md").read_text(encoding="utf-8")
        self.repo.write(
            "adr-0005-a-decision.md", current.replace("Accepted", "Proposed")
        )
        self.repo.commit("back to proposed")
        self.edit("Decision", "Rewritten while still proposed.")
        self.repo.commit("rewrite while proposed")
        self.assertEqual(run_gate(self.repo.root, "HEAD~1..HEAD").returncode, 0)

    # --- the two escapes ----------------------------------------------------
    def test_a_new_adr_naming_it_superseded_permits_the_edit(self):
        self.edit("Decision", "Superseded text, left as the record.")
        self.repo.write(
            "adr-0006-the-successor.md",
            "# ADR-0006: The successor\n\n"
            "- **Status:** Accepted\n"
            "- **Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n\n"
            "## Decision\n\nThe new decision.\n",
        )
        self.repo.commit("supersede ADR-0005")
        self.assertEqual(run_gate(self.repo.root, "HEAD~1..HEAD").returncode, 0)

    # The loose reading this gate refuses: a new ADR is not a blank cheque.
    def test_a_new_adr_naming_a_different_adr_does_not_permit_the_edit(self):
        self.edit("Decision", "Quietly rewritten.")
        self.repo.write(
            "adr-0006-the-successor.md",
            "# ADR-0006: The successor\n\n"
            "- **Status:** Accepted\n"
            "- **Supersedes:** [ADR-0009](adr-0009-elsewhere.md)\n\n"
            "## Decision\n\nAbout something else.\n",
        )
        self.repo.commit("add an unrelated ADR while rewriting 0005")
        outcome = run_gate(self.repo.root, "HEAD~1..HEAD")
        self.assertEqual(outcome.returncode, 1)
        self.assertIn("ADR-0005", outcome.stderr)

    # Prose is not a declaration. ADR-0012 says "an Accepted ADR is superseded
    # by a new record, not edited" two sentences from "ADR-0005", and reading
    # that as a claim excused the very edit this gate refuses.
    def test_prose_mentioning_supersession_does_not_permit_the_edit(self):
        self.edit("Decision", "Quietly rewritten.")
        self.repo.write(
            "adr-0006-the-successor.md",
            "# ADR-0006: The successor\n\n"
            "- **Status:** Accepted\n\n"
            "## Context\n\n"
            "An Accepted ADR is superseded by a new record rather than edited,\n"
            "which is why ADR-0005 matters here.\n\n"
            "## Decision\n\nSomething.\n",
        )
        self.repo.commit("add an ADR that merely discusses supersession")
        self.assertEqual(run_gate(self.repo.root, "HEAD~1..HEAD").returncode, 1)

    def test_marking_it_superseded_in_the_same_change_permits_the_edit(self):
        current = self.repo.path("adr-0005-a-decision.md").read_text(encoding="utf-8")
        self.repo.write(
            "adr-0005-a-decision.md",
            current.replace("- **Status:** Accepted", "- **Status:** Superseded").replace(
                "The decision, as accepted.", "The decision, with a marker above it."
            ),
        )
        self.repo.commit("mark superseded and annotate")
        self.assertEqual(run_gate(self.repo.root, "HEAD~1..HEAD").returncode, 0)

    # --- adding, and the empty range ---------------------------------------
    def test_adding_a_new_adr_is_not_an_edit(self):
        self.repo.write(
            "adr-0006-brand-new.md",
            "# ADR-0006: Brand new\n\n- **Status:** Accepted\n\n"
            "## Decision\n\nNew.\n\n## Consequences\n\n- New.\n",
        )
        self.repo.commit("add an ADR")
        self.assertEqual(run_gate(self.repo.root, "HEAD~1..HEAD").returncode, 0)

    # --- the ways an edit can hide, each found by running the gate ---------

    # `@@ -8 +7,0 @@` gains no line on the new side, so `range(start, start+0)`
    # is empty and the edit records as untouched. Deleting a consequence you no
    # longer like is at least as much a breach as adding one, and quieter.
    def test_deleting_a_line_from_a_frozen_section_fails(self):
        current = self.repo.path("adr-0005-a-decision.md").read_text(encoding="utf-8")
        self.repo.write(
            "adr-0005-a-decision.md", current.replace("- What follows from it.\n", "")
        )
        self.repo.commit("delete a consequence")
        outcome = run_gate(self.repo.root, "HEAD~1..HEAD")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Consequences", outcome.stderr)

    # git reports a rename-with-edit as R, which a --diff-filter=M never saw.
    def test_renaming_the_file_does_not_hide_the_edit(self):
        self.edit("Decision", "Rewritten under a new name.")
        git(self.repo.root, "mv",
            f"{ADR_DIR}/adr-0005-a-decision.md", f"{ADR_DIR}/adr-0005-a-revised-decision.md")
        self.repo.commit("rename and rewrite")
        outcome = run_gate(self.repo.root, "HEAD~1..HEAD")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("ADR-0005", outcome.stderr)

    def test_deleting_an_accepted_adr_outright_fails(self):
        git(self.repo.root, "rm", "-q", f"{ADR_DIR}/adr-0005-a-decision.md")
        self.repo.commit("delete the ADR")
        outcome = run_gate(self.repo.root, "HEAD~1..HEAD")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("deleted while Accepted", outcome.stderr)

    # An example is not a declaration, any more than prose is. An ADR about the
    # ADR process would illustrate the header — and excuse whatever it names.
    def test_a_supersedes_header_inside_a_code_fence_does_not_permit_the_edit(self):
        self.edit("Decision", "Quietly rewritten.")
        self.repo.write(
            "adr-0006-the-successor.md",
            "# ADR-0006: The successor\n\n"
            "- **Status:** Accepted\n\n"
            "## Context\n\n"
            "An ADR declares what it replaces like this:\n\n"
            "```markdown\n"
            "- **Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n"
            "```\n\n"
            "## Decision\n\nSomething.\n",
        )
        self.repo.commit("add an ADR that illustrates the header")
        self.assertEqual(run_gate(self.repo.root, "HEAD~1..HEAD").returncode, 1)

    # A `## Decision` inside a fence must not relocate the frozen span.
    def test_a_heading_inside_a_code_fence_does_not_move_the_frozen_sections(self):
        current = self.repo.path("adr-0005-a-decision.md").read_text(encoding="utf-8")
        self.repo.write(
            "adr-0005-a-decision.md",
            current
            + "\nAn example of the shape:\n\n```markdown\n## Decision\n\nnot real\n```\n",
        )
        self.repo.commit("add a fenced example")
        self.edit("Decision", "Rewritten while a fence claims otherwise.")
        self.repo.commit("rewrite the real decision")
        outcome = run_gate(self.repo.root, "HEAD~1..HEAD")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    # The natural multi-ADR form. Stopping the clause at any bullet made this a
    # false breach — the gate refusing the change it exists to encourage.
    def test_a_supersedes_list_naming_several_adrs_permits_the_edit(self):
        self.edit("Decision", "Superseded text, left as the record.")
        self.repo.write(
            "adr-0006-the-successor.md",
            "# ADR-0006: The successor\n\n"
            "- **Status:** Accepted\n"
            "- **Supersedes:**\n"
            "  - [ADR-0004](adr-0004-earlier.md)\n"
            "  - [ADR-0005](adr-0005-a-decision.md)\n\n"
            "## Decision\n\nThe new decision.\n",
        )
        self.repo.commit("supersede two ADRs at once")
        outcome = run_gate(self.repo.root, "HEAD~1..HEAD")
        self.assertEqual(outcome.returncode, 0, outcome.stdout + outcome.stderr)

    # A one-character header edit would otherwise disable the gate for that
    # file, permanently and silently — story-0704's subject, self-applied.
    def test_an_unreadable_status_is_a_fault_not_a_pass(self):
        current = self.repo.path("adr-0005-a-decision.md").read_text(encoding="utf-8")
        self.repo.write(
            "adr-0005-a-decision.md",
            current.replace("- **Status:** Accepted", "- Status: Accepted"),
        )
        self.repo.commit("reformat the status line")
        self.edit("Decision", "Rewritten behind an unreadable header.")
        self.repo.commit("rewrite the decision")
        outcome = run_gate(self.repo.root, "HEAD~1..HEAD")
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("Status", outcome.stderr)

    def test_a_bare_commit_is_refused_rather_than_diffed_against_the_worktree(self):
        outcome = run_gate(self.repo.root, "HEAD")
        self.assertEqual(outcome.returncode, 2)
        self.assertIn("not a range", outcome.stderr)

    def test_editing_a_file_outside_the_adr_directory_passes(self):
        (self.repo.root / "notes.md").write_text(
            "Nothing to do with ADRs.\n", encoding="utf-8"
        )
        self.repo.commit("add a note")
        self.assertEqual(run_gate(self.repo.root, "HEAD~1..HEAD").returncode, 0)

    def test_a_range_naming_no_commits_is_refused(self):
        outcome = run_gate(self.repo.root, "HEAD..HEAD")
        self.assertEqual(outcome.returncode, 2)
        self.assertIn("empty gate", outcome.stderr)

    # `main...HEAD` is this gate's own default, and splitting on the literal
    # ".." turned it into ".HEAD" — a range naming nothing, reported as an
    # empty gate rather than as the parse failure it was.
    def test_a_three_dot_range_is_read_the_way_git_reads_it(self):
        git(self.repo.root, "checkout", "-q", "-b", "topic")
        self.edit("Decision", "Something else entirely.")
        self.repo.commit("rewrite the decision on a branch")
        outcome = run_gate(self.repo.root, "main...topic")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("ADR-0005", outcome.stderr)

    def test_an_unreadable_range_is_refused_rather_than_passed(self):
        outcome = run_gate(self.repo.root, "no-such-ref..HEAD")
        self.assertEqual(outcome.returncode, 2)


class TheHistoricalBreach(unittest.TestCase):
    """Against this repository, at the commit the gate was written for.

    `4a4221e` wrote the two-tier destination rule into ADR-0005's Consequences
    in place, +14/-2. It is the M6 audit's highest-severity finding, and a gate
    that cannot be shown to catch it is not evidence of anything.
    """

    def test_it_fails_on_4a4221e_and_names_adr_0005(self):
        outcome = run_gate(REPO_ROOT, "4a4221e^..4a4221e")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("ADR-0005", outcome.stderr)
        self.assertIn("Consequences", outcome.stderr)


if __name__ == "__main__":
    unittest.main()
