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
from dataclasses import dataclass

from adr_document import (
    ADR_DIRECTORY,
    FROZEN_SECTIONS,
    CannotAnswer,
    adr_number,
    frozen_spans,
    is_adr_path,
    is_on_the_record,
    names_as_superseded,
    sections_of,
    status_of,
    touches,
)
from adr_range import (
    Change,
    adr_directory_holds_anything,
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


def a_new_record_supersedes_it(
    diff_range: str, changes: list[Change], change: Change, number: str
) -> bool:
    """Whether some ADR *added* by this same change declares it superseded."""
    end = range_end(diff_range)
    for other in changes:
        if other.new != change.new and is_adr_path(other.new) and not other.old:
            if names_as_superseded(text_at(end, other.new), number, other.new):
                return True
    return False


def frozen_sections_touched(diff_range: str, change: Change) -> list[str]:
    """Which frozen sections this change disturbed, judged on both sides.

    The old file decides for removals and the new one for everything else.
    Strict on the file as it now stands, lenient on the pre-image: the old spans
    only attribute removals, and a section the old file never had cannot have
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


def breaches_in(diff_range: str, changes: list[Change], change: Change) -> list[Breach]:
    """What this one record's change earns, if it was on the record when it began.

    The *pre-image* answers "was this frozen", with no fallback when it will not
    parse — the fallback an earlier version had reopened the very escape it was
    meant to close. `Superseded` is frozen too: it is still the record of a
    decision that was taken.

    The escape is the *transition*, not the end state. Read as a property of the
    post-image alone, "its Status becomes `Superseded`" would excuse every later
    edit as well, since the post-image goes on saying `Superseded` forever.
    """
    before = status_of(text_at(range_start(diff_range), change.old), change.old).lower()
    if not is_on_the_record(before):
        return []
    sections = frozen_sections_touched(diff_range, change)
    if not sections:
        return []
    number = adr_number(change.old)
    after = status_of(text_at(range_end(diff_range), change.new), change.new).lower()
    if before == "accepted" and after == "superseded":
        return []
    if a_new_record_supersedes_it(diff_range, changes, change, number):
        return []
    return [Breach(f"ADR-{number}", name) for name in sections]


def removals_in(diff_range: str, changes: list[Change]) -> list[str]:
    """Records this range stops the gate — or a citation — being able to find.

    Deleted outright; moved somewhere the naming convention no longer matches;
    or renumbered, which retires a cited number while touching no line of prose.
    Git reports all three as renames or deletions, so a delete filter saw only
    the first.

    **`Superseded` counts, and that is the whole point.** Guarding only
    `Accepted` left a two-step escape: mark it `Superseded` in one change —
    which passes, and must — then delete it in the next, whose pre-image now
    reads `Superseded`. A draft that was never accepted may still be withdrawn.
    """
    gone: list[str] = []
    for change in changes:
        if not is_adr_path(change.old):
            continue
        if is_adr_path(change.new) and adr_number(change.new) == adr_number(change.old):
            continue
        was = status_of(text_at(range_start(diff_range), change.old), change.old)
        if not is_on_the_record(was):
            continue
        if change.deleted:
            gone.append(f"{change.old} was deleted while {was}")
        elif is_adr_path(change.new):
            gone.append(
                f"{change.old} was renumbered to ADR-{adr_number(change.new)} while {was}"
            )
        else:
            gone.append(
                f"{change.old} was moved to {change.new}, outside the ADR naming "
                f"convention, while {was}"
            )
    return gone


def refuse_malformed_records(diff_range: str, changes: list[Change]) -> None:
    """No range may leave a malformed ADR on the record behind it.

    Everything else here is strict about the post-image and forgiving about the
    pre-image, which only works if a malformed record can never enter. Asked of
    *arrival* rather than of addition, because those differ: a draft may land
    incomplete, and promoting it is a modification, not an addition.
    """
    end = range_end(diff_range)
    for change in changes:
        if not is_adr_path(change.new):
            continue
        text = text_at(end, change.new)
        if is_on_the_record(status_of(text, change.new)):
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
        str(breach)
        for change in edited
        for breach in breaches_in(diff_range, changes, change)
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
    # Only when the range touched nothing here is there a question to answer:
    # "no ADR changed" and "there are no ADRs to change" look identical from
    # outside, and the second means the gate is covering an empty set. When the
    # range *did* touch records, it inspected them — including the range that
    # empties the directory, whose deletions `removals_in` is about to report,
    # and which this guard would otherwise mask with a fault.
    if not changes and not adr_directory_holds_anything(range_end(diff_range)):
        return 2, [
            f"nothing under {ADR_DIRECTORY} at {range_end(diff_range)}; "
            "refusing to pass an empty gate"
        ]
    refuse_malformed_records(diff_range, changes)
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
