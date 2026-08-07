#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a change to one ADR earns: nothing, or a complaint naming the record.

The rule layer of story-0705's stack — `adr_document` says what an ADR is,
`adr_range` says what a range changed, this says what that change means, and
`check_adr_immutability` turns it into an exit code.

`docs/testing/quality-gates.md` states what is refused. The docstrings below
say why each refusal is *shaped* the way it is, which is the question that kept
being got wrong: six escapes worked by splitting a refused edit across two
changes, each step legitimate alone. Both halves of the answer: a decision point
must not read what a neighbouring change can manufacture, and an excuse must not
rest on evidence a later change can destroy.
"""
from __future__ import annotations

from dataclasses import dataclass

from adr_document import (
    FROZEN_SECTIONS,
    adr_number,
    frozen_spans,
    is_adr_path,
    is_on_the_record,
    names_as_superseded,
    sections_of,
    status_of,
    touches,
)
from adr_range import Change, changed_lines, range_end, range_start, text_at


@dataclass(frozen=True)
class Breach:
    """One frozen section of one record on the record, changed with no trace."""

    adr: str
    section: str
    status: str

    def __str__(self) -> str:
        return f"{self.adr}: {self.section} was edited while the ADR is {self.status}"


def a_new_record_supersedes_it(
    diff_range: str, changes: list[Change], change: Change, number: str
) -> bool:
    """Whether an ADR added by this same change, and *on the record*, declares it.

    The declarer's status is the whole weight of the escape. Any added file used
    to do, so a two-line `Proposed` draft unlocked a rewrite — and a draft may be
    freely withdrawn, so the next change erased the trace. Requiring the
    successor to be on the record also brings it under `refuse_malformed_records`
    and `removals_in`. A second file carrying the same number is not a successor.
    """
    end = range_end(diff_range)
    for other in changes:
        if other.old or not is_adr_path(other.new) or other.new == change.new:
            continue
        if adr_number(other.new) == number:
            continue
        declaring = text_at(end, other.new)
        if not is_on_the_record(status_of(declaring, other.new)):
            continue
        if names_as_superseded(declaring, number, other.new):
            return True
    return False


def frozen_sections_touched(diff_range: str, change: Change) -> list[str]:
    """Which frozen sections this change disturbed, judged on both sides.

    Strict on the file as it now stands, lenient on the pre-image: the old spans
    only attribute removals, and a section the old file never had cannot have
    lost anything.
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
    parse — that fallback reopened the escape it was meant to close. `Superseded`
    is frozen too. And the escape is the *transition*, not the end state: read
    off the post-image alone it would excuse every later edit, since the file
    goes on saying `Superseded` forever.
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
    return [Breach(f"ADR-{number}", name, before.capitalize()) for name in sections]
