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
stated once, in `docs/testing/quality-gates.md`; the short version:

- only **Decision** and **Consequences** are frozen, because Status must change
  for a supersession to be recordable at all;
- **the pre-image decides** whether the record was frozen and which record it
  is. Asking the post-image made demoting the Status to `Proposed` in the same
  commit a general-purpose escape hatch, and made a rename off the naming
  convention erase the record from the gate entirely;
- an edit is excused only by a change that *names* the ADR — a new record
  declaring `**Supersedes:** ADR-NNNN`, or its own Status becoming `Superseded`;
- a **removal is never excused**: deleted, moved off the convention, or
  renumbered, the record stops being readable under the number that cited it;
- no range may *leave behind* an unreadable or incomplete `Accepted` ADR, which
  is what stops a malformed record wedging the gate against its own repair;
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
    is_adr_path,
    names_as_superseded,
    sections_of,
    status_of,
    touches,
)
from adr_range import (
    Change,
    changed_lines,
    changes_in,
    commit_count,
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


def superseded_by_the_same_change(diff_range: str, change: Change, number: str) -> bool:
    end = range_end(diff_range)
    for other in changes_in(diff_range):
        if other.new != change.new and is_adr_path(other.new) and not other.old:
            if names_as_superseded(text_at(end, other.new), number):
                return True
    return status_of(text_at(end, change.new), change.new).lower() == "superseded"


def frozen_sections_touched(diff_range: str, change: Change) -> list[str]:
    """Which frozen sections this change disturbed, judged on both sides.

    The old file decides for removals and the new one for everything else, so a
    section rewritten, added to, or emptied all report the same way.

    Strict on the file as it now stands, lenient on the pre-image: the old spans
    only attribute *removals*, and a section the old file never had cannot have
    had anything removed from it.
    """
    old_lines, new_lines = changed_lines(diff_range, change.old, change.new)
    old_spans = sections_of(text_at(range_start(diff_range), change.old), change.old)
    new_spans = frozen_spans(text_at(range_end(diff_range), change.new), change.new)
    return [
        name
        for name in FROZEN_SECTIONS
        if touches(new_spans, name, new_lines) or touches(old_spans, name, old_lines)
    ]


def breaches_in(diff_range: str, change: Change) -> list[Breach]:
    """What this one record's change earns, if it was frozen when it began.

    The *pre-image* answers "was this Accepted". Read from the post-image, one
    commit that demotes the Status and rewrites the Decision passed with exit 0.
    There is no fallback for a pre-image that will not parse: an earlier version
    fell back to the post-image, which reopened that same escape through two
    commits — mangle the header in one, demote and rewrite in the next. Nothing
    can arrive with an unreadable header now, so the fault is the right answer.
    """
    if status_of(text_at(range_start(diff_range), change.old), change.old).lower() != "accepted":
        return []
    sections = frozen_sections_touched(diff_range, change)
    number = adr_number(change.old)
    if not sections or superseded_by_the_same_change(diff_range, change, number):
        return []
    return [Breach(f"ADR-{number}", name) for name in sections]


def removals_in(diff_range: str, changes: list[Change]) -> list[str]:
    """Accepted records this range stops the gate — or a citation — being able to find.

    Deleted outright; moved somewhere the naming convention no longer matches
    (`superseded/`, `.markdown`, a new prefix); or renumbered, which retires a
    number that other documents cite while touching no line of prose. Git
    reports all three as renames or deletions of a path, so a delete filter saw
    only the first.

    With no escape, deliberately. Supersession excuses an *edit* because the
    superseded record survives to be read — that is the whole mechanism. A
    removal destroys that, so a superseding record alongside makes the loss no
    smaller. Mark it `Superseded` and leave it where it is.
    """
    gone: list[str] = []
    for change in changes:
        if not is_adr_path(change.old):
            continue
        if is_adr_path(change.new) and adr_number(change.new) == adr_number(change.old):
            continue
        if status_of(text_at(range_start(diff_range), change.old), change.old).lower() != "accepted":
            continue
        if change.deleted:
            gone.append(f"{change.old} was deleted while Accepted")
        elif is_adr_path(change.new):
            gone.append(
                f"{change.old} was renumbered to ADR-{adr_number(change.new)} while Accepted"
            )
        else:
            gone.append(
                f"{change.old} was moved to {change.new}, outside the ADR naming "
                "convention, while Accepted"
            )
    return gone


def malformed_records(diff_range: str, changes: list[Change]) -> None:
    """No range may leave a malformed `Accepted` ADR behind it.

    Every other reading here is strict about the post-image and forgiving about
    the pre-image, which only works if a malformed record can never enter. The
    first version of this checked *additions*, which is not the same moment: a
    draft may land as incomplete as it likes, and the commit that promotes it to
    `Accepted` then walked it past the gate — after which every edit exited 2
    and the repair exited 1. A status change is an arrival, and so is a rename
    into the convention, so the question is asked of every record the range
    leaves in place.
    """
    end = range_end(diff_range)
    for change in changes:
        if not is_adr_path(change.new):
            continue
        text = text_at(end, change.new)
        if status_of(text, change.new).lower() == "accepted":
            frozen_spans(text, change.new)


def report(diff_range: str, changes: list[Change]) -> list[str]:
    """Every complaint this range earns, as lines ready to print."""
    # An edit is judged by what the record *was*: a rename with an edit is
    # reported as R, and a filter on the new name lost it entirely.
    edited = [
        change
        for change in changes
        if is_adr_path(change.old)
        and is_adr_path(change.new)
        and adr_number(change.old) == adr_number(change.new)
    ]
    complaints = [
        str(breach) for change in edited for breach in breaches_in(diff_range, change)
    ]
    return complaints + removals_in(diff_range, changes)


def verdict(diff_range: str) -> tuple[int, list[str]]:
    """The exit code and what to say, with no I/O of its own."""
    # Parsed for its refusal, not for its parts. `git diff <commit>` compares
    # the working tree, so a bare commit-ish has to be rejected here: the
    # empty-range guard below counts it as a perfectly good range.
    split_range(diff_range)
    if commit_count(diff_range) == 0:
        return 2, [f"{diff_range} names no commits; refusing to pass an empty gate"]
    changes = changes_in(diff_range)
    malformed_records(diff_range, changes)
    complaints = report(diff_range, changes)
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
