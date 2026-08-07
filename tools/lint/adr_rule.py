#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a change to one ADR earns: nothing, or a complaint naming the record.

The rule layer of story-0705's stack — `adr_document` says what an ADR is,
`adr_range` says what a range changed, this says what that change means, and
`check_adr_immutability` turns it into an exit code.

The rule, its escapes and their limits are stated once, in
`docs/testing/quality-gates.md`, and not restated here. The one thing worth
knowing before reading the code: **the pre-image decides** whether a record was
frozen and which record it is. Asking the file as it now stands turned a Status
demotion into a general-purpose escape hatch, twice.
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
from adr_range import (
    Change,
    changed_lines,
    range_end,
    range_start,
    text_at,
)


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
    return [Breach(f"ADR-{number}", name, before.capitalize()) for name in sections]


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


def demotions_in(diff_range: str, changes: list[Change]) -> list[str]:
    """A record leaves `Accepted` only by becoming `Superseded`.

    The Status line is not frozen and must not be — supersession is recorded by
    changing it. But a change that sets `Proposed` takes the record *off* the
    record, and the next change then finds a pre-image that is not frozen and
    may rewrite the decision, empty it, or rename its headings. Two green pull
    requests, and the second needs no cleverness at all.

    This is the fourth escape in this gate built the same way: split a refused
    edit across two changes, each of which is individually legitimate. The
    answer is the same each time — the breach is the step that makes the next
    one possible, judged in the change that makes it.
    """
    start, end = range_start(diff_range), range_end(diff_range)
    left: list[str] = []
    for change in changes:
        if not (is_adr_path(change.old) and is_adr_path(change.new)):
            continue
        if adr_number(change.old) != adr_number(change.new):
            continue
        before = status_of(text_at(start, change.old), change.old)
        after = status_of(text_at(end, change.new), change.new)
        if not is_on_the_record(before) or is_on_the_record(after):
            continue
        left.append(
            f"ADR-{adr_number(change.old)}: Status went {before} → {after}; a record "
            "leaves Accepted only by becoming Superseded"
        )
    return left


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
    return (
        complaints
        + removals_in(diff_range, changes)
        + demotions_in(diff_range, changes)
    )
