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
- **the pre-image decides**: whether the record was frozen, and which record it
  is. Asking the post-image made demoting the Status to `Proposed` in the same
  commit a general-purpose escape hatch, and made a rename off the naming
  convention erase the record from the gate entirely;
- an edit is excused only by a change that *names* the ADR — a new record
  declaring `**Supersedes:** ADR-NNNN`, or its own Status becoming `Superseded`;
- a **removal is never excused**, because the superseded record is the point;
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


def was_frozen(old_text: str, change: Change, new_text: str) -> bool:
    """Whether this record was `Accepted` *before* the change.

    The pre-image owns this question. Read from the post-image instead, one
    commit that demotes the Status to `Proposed` and rewrites the Decision
    passes with exit 0 — a general-purpose escape hatch reachable by anyone
    editing the file they are already editing, which is exactly the `--allow`
    flag this gate refused to have.

    A pre-image that cannot be read falls back to the post-image rather than
    failing, because history cannot be repaired: an ADR that landed malformed
    would otherwise leave the file with no green state at all, not even the
    commit that fixes it. Nothing can land malformed any more — see
    `refuse_malformed_additions` — so this covers records older than the gate.
    """
    try:
        return status_of(old_text, change.old).lower() == "accepted"
    except CannotAnswer:
        return status_of(new_text, change.new).lower() == "accepted"


def superseded_by_the_same_change(diff_range: str, change: Change, number: str) -> bool:
    end = range_end(diff_range)
    for added in changes_in(diff_range):
        if added.added and is_adr_path(added.new):
            if names_as_superseded(text_at(end, added.new), number):
                return True
    return status_of(text_at(end, change.new), change.new).lower() == "superseded"


def frozen_sections_touched(diff_range: str, change: Change) -> list[str]:
    """Which frozen sections this change disturbed, judged on both sides.

    The old file decides for removals and the new one for everything else, so a
    section rewritten, added to, or emptied all report the same way.

    Strict on the file as it now stands, lenient on the pre-image: the old spans
    only attribute *removals*, and a section the old file never had cannot have
    had anything removed from it. The new-side check is what closes the hole —
    renaming `## Decision` leaves the file unreadable in every later range too,
    not only in the one that renamed it.
    """
    old_lines, new_lines = changed_lines(diff_range, change.old, change.new)
    new_text = text_at(range_end(diff_range), change.new)
    try:
        old_spans = sections_of(text_at(range_start(diff_range), change.old), change.old)
    except CannotAnswer:
        old_spans = {}
    new_spans = frozen_spans(new_text, change.new)
    return [
        name
        for name in FROZEN_SECTIONS
        if touches(new_spans, name, new_lines) or touches(old_spans, name, old_lines)
    ]


def breaches_in(diff_range: str, change: Change) -> list[Breach]:
    old_text = text_at(range_start(diff_range), change.old)
    new_text = text_at(range_end(diff_range), change.new)
    if not was_frozen(old_text, change, new_text):
        return []
    sections = frozen_sections_touched(diff_range, change)
    number = adr_number(change.old)
    if not sections or superseded_by_the_same_change(diff_range, change, number):
        return []
    return [Breach(f"ADR-{number}", name) for name in sections]


def removed_records(diff_range: str, changes: list[Change]) -> list[str]:
    """Accepted ADRs this range takes out of the gate's sight, however.

    Deleted outright, or renamed somewhere the naming convention no longer
    matches — `superseded/adr-0005-….md`, `.markdown`, a new prefix. Git reports
    the second as a rename, so a delete filter never saw it and the record left
    the gate silently while the docs claimed deletion was refused.

    With no escape, deliberately. Supersession excuses an *edit* because the
    superseded record survives to be read — that is the whole mechanism. A
    removal destroys it, so a superseding record alongside makes the loss no
    smaller. Mark it `Superseded` and leave it where it is.
    """
    gone: list[str] = []
    for change in changes:
        if not is_adr_path(change.old) or is_adr_path(change.new):
            continue
        if status_of(text_at(range_start(diff_range), change.old), change.old).lower() != "accepted":
            continue
        gone.append(
            f"{change.old} was deleted while Accepted"
            if change.deleted
            else f"{change.old} was moved to {change.new}, outside the ADR naming "
            "convention, while Accepted"
        )
    return gone


def refuse_malformed_additions(diff_range: str, changes: list[Change]) -> None:
    """An ADR may not *land* unreadable, which is what keeps the gate unwedgeable.

    Every other reading here is strict about the post-image and forgiving about
    the pre-image. That only works if a malformed record can never enter: added
    files were parsed by nothing, so one landing with an unclosed fence made
    every later range over it exit 2 — including the commit that would repair it.
    Checked when it arrives, when it can still be fixed.
    """
    end = range_end(diff_range)
    for change in changes:
        if change.added and is_adr_path(change.new):
            text = text_at(end, change.new)
            if status_of(text, change.new).lower() == "accepted":
                frozen_spans(text, change.new)


def report(diff_range: str) -> list[str]:
    """Every complaint this range earns, as lines ready to print."""
    changes = changes_in(diff_range)
    refuse_malformed_additions(diff_range, changes)
    # An edit is judged by what the record *was*: a rename with an edit is
    # reported as R, and a filter on the new name lost it entirely.
    edited = [
        change
        for change in changes
        if is_adr_path(change.old) and is_adr_path(change.new)
    ]
    complaints = [
        str(breach) for change in edited for breach in breaches_in(diff_range, change)
    ]
    return complaints + removed_records(diff_range, changes)


def verdict(diff_range: str) -> tuple[int, list[str]]:
    """The exit code and what to say, with no I/O of its own."""
    # Parsed for its refusal, not for its parts. `git diff <commit>` compares
    # the working tree, so a bare commit-ish has to be rejected here: the
    # empty-range guard below counts it as a perfectly good range.
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
