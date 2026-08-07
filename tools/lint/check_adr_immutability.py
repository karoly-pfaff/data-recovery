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
what a range changed is `adr_range`. The rule, its escapes and their limits are
stated once, in `docs/testing/quality-gates.md`, and not restated here. The two
things worth knowing before reading the code:

- **the pre-image decides** whether the record was frozen and which record it
  is — asking the post-image turned a Status demotion into an escape hatch;
- anything unreadable is a fault (exit 2), never a pass, and it catches an
  *edit* rather than an *inaccuracy*.
"""
from __future__ import annotations

import argparse
import logging
import sys

from adr_document import ADR_DIRECTORY, CannotAnswer
from adr_range import adr_paths_at, changes_in, commit_count, range_end, split_range
from adr_rule import refuse_malformed_records, report


def verdict(diff_range: str) -> tuple[int, list[str]]:
    """The exit code and what to say, with no I/O of its own."""
    # Parsed for its refusal, not for its parts. `git diff <commit>` compares
    # the working tree, so a bare commit-ish has to be rejected here: the
    # empty-range guard below counts it as a perfectly good range.
    split_range(diff_range)
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
