#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The cases that read *this* repository rather than a throwaway one.

Everything else builds its own history, so the cases stay stable while the real
ADRs change. These cannot: a gate written to catch one commit is unverified
until it has been run against that commit; the ordering constraint this story
turns on is a claim about a commit on `main`; and the invariant the pre-image
leniency rests on is a claim about the records that are here now.
"""
from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "tools" / "lint"))

from adr_document import frozen_spans, is_on_the_record, status_of  # noqa: E402
from adr_fixture import ADR_DIR, REPO_ROOT, run_gate  # noqa: E402


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


class TheOrderingConstraint(unittest.TestCase):
    """story-0701's own commit, which is why this story lands after it.

    The story argued for sequencing over an escape flag on the strength of this
    range; it was recorded as prose, with the reason that a merge state cannot
    be pinned. That was wrong — `479ccd2` is the squash commit on `main`, as
    stable as any other, and the claim it supports is the story's central design
    decision. Both halves matter: ADR-0011 excused because ADR-0012 declares it
    superseded, ADR-0005's restore refused because nothing declares *it* so.
    """

    RANGE = "479ccd2^..479ccd2"

    def test_it_refuses_the_restore_and_excuses_adr_0011(self):
        outcome = run_gate(REPO_ROOT, self.RANGE)
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("ADR-0005", outcome.stderr)
        self.assertIn("Consequences", outcome.stderr)
        self.assertNotIn("ADR-0011", outcome.stderr)


class TheRecordsAlreadyHere(unittest.TestCase):
    """Every ADR in the tree parses the way the gate needs it to.

    The whole design leans on this and nothing asserted it. The arrival check
    only sees records a range touches, so the twelve that predate the gate never
    passed through it — and the pre-image leniency everywhere else is only safe
    because a malformed record cannot exist. Verified by hand once, during the
    audit that asked for it, which is exactly the kind of evidence this
    milestone exists to replace with a test.
    """

    def test_the_records_already_here_all_parse(self):
        adrs = sorted((REPO_ROOT / ADR_DIR).glob("adr-*.md"))
        self.assertGreaterEqual(len(adrs), 12, "the glob found no records to check")
        for path in adrs:
            with self.subTest(adr=path.name):
                text = path.read_text(encoding="utf-8")
                status = status_of(text, path.name)
                if is_on_the_record(status):
                    frozen_spans(text, path.name)


if __name__ == "__main__":
    unittest.main()
