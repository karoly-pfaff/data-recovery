#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail the build when an `Accepted` ADR's decision is edited in place.

[ADR-0001](../../docs/architecture/adr/adr-0001-record-architecture-decisions.md)
and the ADR index both say it: an ADR is immutable once Accepted, and a later
record supersedes an earlier one rather than editing it. Nothing enforced that,
and M6 broke it in the same commit range that documented it — `4a4221e` wrote
the two-tier destination rule into ADR-0005's Consequences in place, +14/−2.
That is the M6 audit's highest-severity finding, and it is entirely mechanical
to catch.

This module is the *rule*. What an ADR says about itself is `adr_document`;
what a range changed is `adr_range`. The rule, its two escapes and their limits
are stated once, in `docs/testing/quality-gates.md`; the short version:

- only **Decision** and **Consequences** are frozen, because Status must change
  for a supersession to be recordable at all;
- an edit is excused only by a change that *names* the ADR — a new record
  declaring `**Supersedes:** ADR-NNNN`, or its own Status becoming `Superseded`;
- a **deletion is never excused**, because the superseded record is the point;
- anything unreadable is a fault (exit 2), never a pass;
- it catches an *edit*, not an *inaccuracy*.
"""
from __future__ import annotations

import argparse
import logging
import sys
from dataclasses import dataclass

from adr_document import (
    FROZEN_SECTIONS,
    CannotAnswer,
    adr_number,
    frozen_spans,
    names_as_superseded,
    sections_of,
    status_of,
    touches,
)
from adr_range import (
    adr_paths,
    changed_lines,
    commit_count,
    previous_path,
    range_end,
    range_start,
    split_range,
    text_at,
)


@dataclass(frozen=True)
class Breach:
    """One frozen section of one Accepted ADR, changed with no trace."""

    adr: str
    section: str

    def __str__(self) -> str:
        return f"{self.adr}: {self.section} was edited while the ADR is Accepted"


def superseded_by_the_same_change(diff_range: str, path: str, number: str) -> bool:
    end = range_end(diff_range)
    for added in adr_paths(diff_range, "A"):
        if names_as_superseded(text_at(end, added), number):
            return True
    return status_of(text_at(end, path), path).lower() == "superseded"


def frozen_sections_touched(diff_range: str, path: str) -> list[str]:
    """Which frozen sections this change disturbed, judged on both sides.

    The old file decides for removals and the new one for everything else, so a
    section rewritten, added to, or emptied all report the same way.
    """
    new_text = text_at(range_end(diff_range), path)
    if status_of(new_text, path).lower() != "accepted":
        return []

    old_text = text_at(range_start(diff_range), previous_path(diff_range, path))
    old_lines, new_lines = changed_lines(diff_range, path)
    # Strict on the file as it now stands, lenient on the pre-image. The old
    # spans only judge *removals*, and a section the old file never had cannot
    # have had anything removed from it — while demanding both there would fail
    # the repair that adds a missing Consequences. The new-side check is what
    # closes the hole: renaming `## Decision` leaves the file unreadable in
    # every later range too, not just the one that renamed it.
    old_spans = sections_of(old_text, path)
    new_spans = frozen_spans(new_text, path)
    return [
        name
        for name in FROZEN_SECTIONS
        if touches(new_spans, name, new_lines) or touches(old_spans, name, old_lines)
    ]


def breaches_in(diff_range: str, path: str) -> list[Breach]:
    sections = frozen_sections_touched(diff_range, path)
    number = adr_number(path)
    if not sections or superseded_by_the_same_change(diff_range, path, number):
        return []
    return [Breach(f"ADR-{number}", name) for name in sections]


def deleted_adrs(diff_range: str) -> list[str]:
    """An Accepted ADR removed outright is the same breach, more thoroughly.

    With no escape, deliberately. Supersession excuses an *edit* because the
    superseded record survives to be read — that is the whole mechanism. A
    deletion destroys it, so a superseding record alongside makes the loss no
    smaller. Mark it `Superseded` and leave it where it is.
    """
    return [
        path
        for path in adr_paths(diff_range, "D")
        if status_of(text_at(range_start(diff_range), path), path).lower() == "accepted"
    ]


def report(diff_range: str) -> list[str]:
    """Every complaint this range earns, as lines ready to print."""
    # M and R both mean "this file was edited": a rename with an edit is
    # reported as R, and filtering to M alone let it through untouched.
    complaints = [
        str(breach)
        for path in adr_paths(diff_range, "MR")
        for breach in breaches_in(diff_range, path)
    ]
    return complaints + [
        f"{path} was deleted while Accepted" for path in deleted_adrs(diff_range)
    ]


def verdict(diff_range: str) -> tuple[int, list[str]]:
    """The exit code and what to say, with no I/O of its own."""
    # Parsed for its refusal, not for its parts. `git diff <commit>` compares
    # the working tree, so a bare commit-ish has to be rejected here: `rev-list`
    # counts it as a perfectly good range and the empty-range guard passes it.
    split_range(diff_range)
    if commit_count(diff_range) == 0:
        return 2, [f"{diff_range} names no commits; refusing to pass an empty gate"]
    complaints = report(diff_range)
    if complaints:
        return 1, complaints + [
            "an Accepted ADR is superseded by a new record, not edited (ADR-0001)."
        ]
    return 0, []


def main() -> int:
    parser = argparse.ArgumentParser(description="Refuse an in-place edit to an Accepted ADR.")
    parser.add_argument("range", nargs="?", default="main...HEAD")
    args = parser.parse_args()
    logging.basicConfig(format="%(message)s", stream=sys.stderr)

    try:
        code, complaints = verdict(args.range)
    except CannotAnswer as fault:
        logging.error("adr gate: %s", fault)
        return 2

    for complaint in complaints:
        logging.error("adr gate: %s", complaint)
    if code == 0:
        print(f"adr gate: no Accepted ADR was edited in place over {args.range}")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
