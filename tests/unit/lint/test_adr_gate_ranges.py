#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a range changed: renames, removals, range syntax, and git's own config.

The rule these feed is `test_check_adr_immutability.py`; the fixtures are
`adr_fixture.py`. Every case here was a silent pass at some point in this
story, and each is written so that reverting its fix turns it red.
"""
from __future__ import annotations

import sys
import unittest

from adr_fixture import ADR_DIR, REPO_ROOT, THE_ADR, AdrGateTest, git, run_gate, successor


class Renames(AdrGateTest):
    # git reports a rename-with-edit as R, which a --diff-filter=M never saw.
    def test_renaming_the_file_does_not_hide_the_edit(self):
        self.edit("Decision", "Rewritten under a new name.")
        self.rename_to("adr-0005-a-revised-decision.md")
        self.repo.commit("rename and rewrite")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)
        # And *only* Decision. Asserting rc == 1 alone stayed green while the
        # diff pathspec named only the new path, so git could not pair the
        # rename, rendered the file as freshly added, and reported every line as
        # touched — including Consequences, which nothing had touched.
        self.assertNotIn("Consequences", outcome.stderr)

    # Correcting a slug must not require declaring the ADR superseded, or the
    # gate trains people to route around it.
    def test_a_rename_with_no_content_change_passes(self):
        self.rename_to("adr-0005-a-better-slug.md")
        self.repo.commit("rename only")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 0, outcome.stdout + outcome.stderr)

    def test_a_rename_with_only_a_context_edit_passes(self):
        self.edit("Context", "A fuller account of why.")
        self.rename_to("adr-0005-a-better-slug.md")
        self.repo.commit("rename and expand the context")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 0, outcome.stdout + outcome.stderr)


class Removals(AdrGateTest):
    def test_deleting_an_accepted_adr_outright_fails(self):
        git(self.repo.root, "rm", "-q", f"{ADR_DIR}/{THE_ADR}")
        self.repo.commit("delete the ADR")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("deleted while Accepted", outcome.stderr)

    # Supersession excuses an *edit* because the superseded record survives to
    # be read — that is the whole mechanism. A deletion destroys it, so the
    # successor makes the loss no smaller. The absence of an escape here is a
    # decision, not an omission, and this is where it is written down.
    def test_a_superseding_record_does_not_excuse_the_deletion(self):
        git(self.repo.root, "rm", "-q", f"{ADR_DIR}/{THE_ADR}")
        self.repo.write(
            "adr-0006-the-successor.md",
            successor("- **Supersedes:** [ADR-0005](adr-0005-a-decision.md)\n"),
        )
        self.repo.commit("replace ADR-0005 with its successor")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("deleted while Accepted", outcome.stderr)

    # git pairs a same-content move as a rename, so a delete filter never saw
    # it, and the *new* name no longer matches the ADR pattern — so a filter on
    # the new name dropped it too. The record left the gate in silence while
    # the documentation claimed deletion was refused.
    def test_moving_an_accepted_adr_out_of_the_naming_convention_is_a_removal(self):
        for destination in (
            "superseded/adr-0005-a-decision.md",
            "adr-0005-a-decision.markdown",
            "old-adr-0005-a-decision.md",
        ):
            with self.subTest(destination=destination):
                self.reset()
                target = self.repo.path(destination)
                target.parent.mkdir(parents=True, exist_ok=True)
                git(self.repo.root, "mv", f"{ADR_DIR}/{THE_ADR}", f"{ADR_DIR}/{destination}")
                self.repo.commit("move it out of the way")
                outcome = self.gate()
                self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
                # The specific message: "while Accepted" alone also matches the
                # deletion wording, so a regression that reported a move as a
                # deletion would stay green — and telling those two apart is the
                # whole point of this case.
                self.assertIn("outside the ADR naming convention", outcome.stderr)
                self.assertIn(destination, outcome.stderr)

    # Renumbering touches no line of prose, so nothing was reported: ADR-0005
    # simply ceased to exist under the number every citation to it uses, with
    # the gate green. A rename is "an edit of the same record" only while it
    # stays the same record.
    def test_renumbering_an_accepted_adr_is_a_removal(self):
        self.rename_to("adr-0099-a-decision.md")
        self.repo.commit("renumber it")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("renumbered to ADR-0099", outcome.stderr)


class RangeSyntax(AdrGateTest):
    def test_a_bare_commit_is_refused_rather_than_diffed_against_the_worktree(self):
        outcome = self.gate("HEAD")
        self.assertEqual(outcome.returncode, 2)
        self.assertIn("not a range", outcome.stderr)

    def test_a_range_naming_no_commits_is_refused(self):
        outcome = self.gate("HEAD..HEAD")
        self.assertEqual(outcome.returncode, 2)
        self.assertIn("empty gate", outcome.stderr)

    def test_an_unreadable_range_is_refused_rather_than_passed(self):
        self.assertEqual(self.gate("no-such-ref..HEAD").returncode, 2)

    # `main...HEAD` is this gate's own default, and splitting on the literal
    # ".." turned it into ".HEAD" — a range naming nothing, reported as an
    # empty gate rather than as the parse failure it was.
    def test_a_three_dot_range_is_read_the_way_git_reads_it(self):
        git(self.repo.root, "checkout", "-q", "-b", "topic")
        self.edit("Decision", "Something else entirely.")
        self.repo.commit("rewrite the decision on a branch")
        outcome = self.gate("main...topic")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("ADR-0005", outcome.stderr)

    # `rev-list --count a...b` counts the symmetric difference; `git diff a...b`
    # reads `merge-base(a,b)..b`. They disagree exactly when the guard matters,
    # so a reversed or stale range expression was reported as a clean pass over
    # a diff that read nothing — this gate's own subject, in its own guard.
    def test_a_three_dot_range_whose_diff_reads_nothing_is_refused(self):
        for number in range(3):
            (self.repo.root / "notes.md").write_text(f"note {number}\n", encoding="utf-8")
            self.repo.commit(f"note {number}")
        counted = git(self.repo.root, "rev-list", "--count", "HEAD...HEAD~3").strip()
        self.assertEqual(counted, "3", "the fixture must be a range rev-list calls non-empty")
        diffed = git(self.repo.root, "diff", "--name-only", "HEAD...HEAD~3").strip()
        self.assertEqual(diffed, "", "…and one git diff reads nothing from")
        outcome = self.gate("HEAD...HEAD~3")
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("empty gate", outcome.stderr)

    # The old side of `a...b` is the merge base, not `a`. Reading it from `a`
    # works only while the two coincide; three dots is the default and what CI
    # passes, so a branch behind `main` is the normal case, not an edge one.
    def test_a_deletion_is_caught_on_a_three_dot_range_when_main_is_ahead(self):
        git(self.repo.root, "checkout", "-q", "-b", "topic")
        self.substitute("- What follows from it.\n", "")
        self.repo.commit("delete a consequence on the branch")

        git(self.repo.root, "checkout", "-q", "main")
        self.substitute("Why.\n", "Why.\n" + "more.\n" * 20)
        self.repo.commit("main grows its context")
        git(self.repo.root, "checkout", "-q", "topic")

        outcome = self.gate("main...topic")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Consequences", outcome.stderr)


class WhatTheRepositoryCanTellGitToDo(AdrGateTest):
    """A gate whose verdict moves with config or cwd is not a gate.

    Each case below made the gate exit **0** on an edit it must catch, and two
    of them were reproduced against the real `4a4221e`. Each is answered by one
    flag in `adr_range.DIFF_FLAGS`, or by running git from the top level;
    remove that one thing and the matching test here fails.
    """

    def rewrite_the_decision(self) -> None:
        self.edit("Decision", "Rewritten in place.")
        self.repo.commit("rewrite the decision")
        self.assertEqual(self.gate().returncode, 1, "the fixture must fail to begin with")

    # `--text`. One in-tree `.gitattributes` line and `git diff` prints
    # "Binary files differ" with no `@@` headers at all, so every hunk set comes
    # back empty. No user configuration is involved: the file is committed, and
    # nothing else in the tree inspects it.
    def test_an_attribute_marking_the_adrs_binary_does_not_hide_the_edit(self):
        self.rewrite_the_decision()
        (self.repo.root / ".gitattributes").write_text(
            "docs/architecture/adr/*.md -diff\n", encoding="utf-8"
        )
        self.repo.commit("mark the ADRs as binary")
        outcome = self.gate("HEAD~2..HEAD")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    # `--no-ext-diff`. An external driver replaces the patch wholesale.
    def test_an_external_diff_driver_does_not_hide_the_edit(self):
        self.rewrite_the_decision()
        git(self.repo.root, "config", "diff.external", "true")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)

    # `--no-textconv`. A filter that drops lines renumbers the whole file, so
    # the hunk headers name lines that are in a different section — or in none.
    def test_a_textconv_filter_does_not_move_the_line_numbers(self):
        stripper = self.repo.root / "strip.py"
        stripper.write_text(
            "import sys\n"
            "print(''.join(l for l in open(sys.argv[1], encoding='utf-8') if l.strip()))\n",
            encoding="utf-8",
        )
        git(self.repo.root, "config", "diff.stripped.textconv", f"{sys.executable} {stripper}")
        (self.repo.root / ".gitattributes").write_text(
            "docs/architecture/adr/*.md diff=stripped\n", encoding="utf-8"
        )
        self.repo.commit("install a textconv filter")
        self.edit("Decision", "Rewritten in place.")
        self.repo.commit("rewrite the decision")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)

    # `-M`. With renames off, a pure slug correction is reported as a delete
    # plus an add, and fails as "deleted while Accepted".
    def test_rename_detection_being_off_does_not_invent_a_deletion(self):
        git(self.repo.root, "config", "diff.renames", "false")
        self.rename_to("adr-0005-a-better-slug.md")
        self.repo.commit("pure rename")
        outcome = self.gate()
        self.assertEqual(outcome.returncode, 0, outcome.stdout + outcome.stderr)

    # The pathspec is relative to the working directory, so from anywhere but
    # the root it matched nothing — while the range stayed non-empty, so the
    # vacuity guard was satisfied and the gate reported a clean pass. This one
    # needed no configuration at all, only a different `cd`.
    def test_running_from_a_subdirectory_does_not_hide_the_edit(self):
        self.rewrite_the_decision()
        elsewhere = self.repo.root / ADR_DIR
        outcome = self.gate(cwd=elsewhere)
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("Decision", outcome.stderr)


class OutsideTheAdrDirectory(AdrGateTest):
    def test_editing_a_file_outside_the_adr_directory_passes(self):
        (self.repo.root / "notes.md").write_text("Nothing to do with ADRs.\n", encoding="utf-8")
        self.repo.commit("add a note")
        self.assertEqual(self.gate().returncode, 0)


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
