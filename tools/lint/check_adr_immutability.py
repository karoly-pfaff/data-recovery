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

This module composes the rules and turns them into an exit code. The stack it
sits on, stated **here and nowhere else** — four earlier docstrings each carried
a copy and each went stale at a different split:

| Module | The question it answers |
|---|---|
| `adr_markdown` | which bytes of a file a *reader* actually sees |
| `adr_document` | what an ADR *is*, and what one record says about itself |
| `adr_git` | running git, and the flags that decide what it will say |
| `adr_range` | what a git range *changed* |
| `adr_rule` | may this *edit* stand — the frozen sections and their escapes |
| `adr_standing` | what may not be done to a record that already *counts* |
| this one | which of those apply, and what the build should do about it |

`docs/testing/quality-gates.md` states the rule itself. Two things worth knowing
before reading any of it: **the pre-image decides** whether a record was frozen
and which record it is; and anything unreadable is a fault (exit 2), never a
pass — this catches an *edit*, not an *inaccuracy*.
"""
from __future__ import annotations

import argparse
import logging
import sys

from adr_document import ADR_DIRECTORY, same_record
from adr_markdown import CannotAnswer
from adr_range import Change, adr_paths_at, changes_in, commit_count, range_end
from adr_rule import breaches_in
from adr_standing import (
    demotions_in,
    refuse_malformed_records,
    removals_in,
    withdrawn_declarations_in,
)


def report(diff_range: str, changes: list[Change]) -> list[str]:
    """The complaints that mean exit 1, as lines ready to print.

    Not every complaint: the faults that mean exit 2 are raised rather than
    returned, by `refuse_malformed_records` and by the readers underneath.
    """
    # An edit is judged by what the record *was*: a rename with an edit is
    # reported as R, and a filter on the new name lost it entirely.
    edited = [change for change in changes if same_record(change.old, change.new)]
    complaints = [
        str(breach)
        for change in edited
        for breach in breaches_in(diff_range, changes, change)
    ]
    return (
        complaints
        + removals_in(diff_range, changes)
        + demotions_in(diff_range, changes)
        + withdrawn_declarations_in(diff_range, changes)
    )


def verdict(diff_range: str) -> tuple[int, list[str]]:
    """The exit code and what to say, with no I/O of its own."""
    if commit_count(diff_range) == 0:
        return 2, [f"{diff_range} names no commits; refusing to pass an empty gate"]
    changes = changes_in(diff_range)
    refuse_malformed_records(diff_range, changes)
    complaints = report(diff_range, changes)
    if complaints:
        return 1, complaints + [
            "an Accepted ADR is superseded by a new record, not edited (ADR-0001)."
        ]
    # Asked last, and only of an otherwise-clean run: "no ADR changed" and
    # "there are no ADRs to change" are indistinguishable from outside, and the
    # second means this gate covered nothing. Asked earlier it would mask the
    # range that legitimately empties the directory by reporting its removals;
    # asked as "does the directory hold a file" rather than "does it hold a
    # record", `README.md` alone satisfied it.
    if not adr_paths_at(range_end(diff_range)):
        return 2, [
            f"no ADRs under {ADR_DIRECTORY} at {range_end(diff_range)}; "
            "refusing to pass an empty gate"
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
