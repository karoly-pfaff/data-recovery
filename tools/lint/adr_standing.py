#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What may not be done to a record that already counts.

`adr_rule` asks whether an *edit* may stand. This asks whether a record may be
removed, demoted, or stripped of the declaration an earlier edit rested on. They
separated because every escape found after the fifth audit round was in this
family and none in that one: the frozen-section rule was right early, and the
record's *standing* was the soft ground.

Four rules, one shape: something that may not be done to a record which was on
the record when the range began. `records_on_the_record` is that shape.
"""
from __future__ import annotations

from collections.abc import Iterator

from adr_document import (
    adr_number,
    declared_superseded,
    frozen_spans,
    is_adr_path,
    is_on_the_record,
    same_record,
    status_of,
)
from adr_range import Change, range_end, range_start, text_at


def records_on_the_record(
    diff_range: str, changes: list[Change]
) -> Iterator[tuple[Change, str]]:
    """Each changed record that was on the record when the range began, and how.

    A removal, a demotion and a withdrawn declaration are three things that may
    not be done to a record that already counted, so all three start here.
    """
    start = range_start(diff_range)
    for change in changes:
        if not is_adr_path(change.old):
            continue
        was = status_of(text_at(start, change.old), change.old)
        if is_on_the_record(was):
            yield change, was


def removals_in(diff_range: str, changes: list[Change]) -> list[str]:
    """Records this range stops the gate — or a citation — being able to find.

    Deleted outright; moved off the naming convention; or renumbered, which
    retires a cited number while touching no line of prose. Git reports all
    three as renames or deletions, so a delete filter saw only the first.

    **`Superseded` counts, and that is the point.** Guarding only `Accepted`
    left a two-step escape: mark it superseded — which passes, and must — then
    delete it. A draft that was never accepted may still be withdrawn.
    """
    gone: list[str] = []
    for change, was in records_on_the_record(diff_range, changes):
        if same_record(change.old, change.new):
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
    """A record on the record may only move to `Superseded`.

    The Status line is not frozen and must not be — supersession is recorded by
    changing it. Every other destination takes the record off the record, after
    which its pre-image is no longer frozen and the decision can be rewritten.

    `Superseded` -> `Accepted` is the same hole from the other side: the
    transition escape in `breaches_in` is granted once, and re-accepting makes
    the grant renewable. Four commits and the record is back where it started
    with a different Decision and no successor in the tree.
    """
    end = range_end(diff_range)
    left: list[str] = []
    for change, before in records_on_the_record(diff_range, changes):
        if not same_record(change.old, change.new):
            continue
        after = status_of(text_at(end, change.new), change.new)
        if after.lower() in (before.lower(), "superseded"):
            continue
        left.append(
            f"ADR-{adr_number(change.old)}: Status went from {before} to {after}; "
            "a record on the record may only move to Superseded"
        )
    return left


def withdrawn_declarations_in(diff_range: str, changes: list[Change]) -> list[str]:
    """A record on the record may not stop declaring what it supersedes.

    `**Supersedes:**` is a header bullet, outside both frozen sections, so a
    later change could simply delete it: a successor was admitted, spent its
    excuse on a rewrite, and had its declaration tidied away — leaving the
    rewritten Decision with nothing pointing at it.

    The general form is what the five fixes before this one missed: **an
    escape's evidence must be as durable as the thing it excuses.** They asked
    what a *previous* change could manufacture; none asked what a *later* one
    could destroy. Adding declarations stays free; dropping one is refused.
    """
    start, end = range_start(diff_range), range_end(diff_range)
    withdrawn: list[str] = []
    for change, _ in records_on_the_record(diff_range, changes):
        if not same_record(change.old, change.new):
            continue
        dropped = declared_superseded(
            text_at(start, change.old), change.old
        ) - declared_superseded(text_at(end, change.new), change.new)
        withdrawn.extend(
            f"ADR-{adr_number(change.old)} no longer declares that it supersedes "
            f"ADR-{number}; that declaration is the trace an edit rests on"
            for number in sorted(dropped)
        )
    return withdrawn


def refuse_malformed_records(diff_range: str, changes: list[Change]) -> None:
    """No range may leave a malformed ADR on the record behind it.

    Everything else is strict about the post-image and forgiving about the
    pre-image, which only works if a malformed record can never enter. Asked of
    *arrival*, not of addition: a draft may land incomplete, and promoting it is
    a modification.
    """
    end = range_end(diff_range)
    for change in changes:
        if not is_adr_path(change.new):
            continue
        text = text_at(end, change.new)
        if is_on_the_record(status_of(text, change.new)):
            frozen_spans(text, change.new)
