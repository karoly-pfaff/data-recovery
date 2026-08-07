#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What one ADR is, and what it says about itself.

The vocabulary of story-0705's stack: where records live, what a path must look
like, and what one record declares — its status, its sections, what it
supersedes. `adr_markdown` is the layer below, which decides what part of the
file a reader actually sees; everything here reads only that.
`check_adr_immutability` holds the map of which module answers what.
"""
from __future__ import annotations

import re

from adr_markdown import CannotAnswer, lines_of, visible_prose

# The sections an Accepted ADR may not have rewritten under it. Status is not
# among them: it has to change for a supersession to be recordable at all.
FROZEN_SECTIONS = ("Decision", "Consequences")

# Stated once. `adr_range` needs the directory as a pathspec and the gate needs
# the pattern; spelling the path in both is one fact in two places, and moving
# the ADRs would then need both changed.
ADR_DIRECTORY = "docs/architecture/adr/"
ADR_PATH = re.compile(rf"^{re.escape(ADR_DIRECTORY)}adr-(\d{{4}})-[a-z0-9-]+\.md$")


SECTION_HEADING = re.compile(r"^##\s+(.+?)\s*$")

# Anchored at the margin, with at most a bullet before it. A four-space indent
# is a markdown code block — the fence's shabby cousin, and not something
# `visible_prose` can blank, because in a list the same indent is continuation
# and the nested `**Supersedes:**` form depends on it. So the field itself must
# sit where a header field sits, which every ADR's does.
STATUS_LINE = re.compile(r"^(?:[-*]\s*)?\*\*Status:?\*\*[:\s]*([A-Za-z]+)")

# A supersession is *declared*, in the header field, not mentioned in passing.
# ADR-0012's Context says "an Accepted ADR is superseded by a new record, not
# edited" two sentences from "ADR-0005", and reading any nearby mention as a
# claim excused precisely the edit this gate refuses.
SUPERSEDES_FIELD = re.compile(
    r"^(?:[-*]\s*)?\*\*Supersedes:?\*\*", re.IGNORECASE | re.MULTILINE
)
ADR_REFERENCE = re.compile(r"ADR-(\d{4})")

# Where a `**Supersedes:**` clause stops: the next *top-level* bullet or a blank
# line. Not a character count — a fixed window swallows the following bullet and
# would excuse an edit to the ADR named there. Indented bullets continue the
# clause, so the natural multi-ADR form is a nested list rather than a refusal.
CLAUSE_END = re.compile(r"\n(?=[-*]\s)|\n\s*\n")


# A record leaves draft when it is Accepted and stays on the record once it is
# Superseded — surviving to be read is the entire point of superseding rather
# than deleting. Both are immutable; only a draft may still be reshaped.
ON_THE_RECORD = ("accepted", "superseded")


def is_on_the_record(status: str) -> bool:
    return status.lower() in ON_THE_RECORD


def is_adr_path(path: str) -> bool:
    return bool(path) and ADR_PATH.match(path) is not None


def same_record(old: str, new: str) -> bool:
    """Whether both sides of a change are the same ADR under the same number.

    Three rules needed this and each spelled it out again, so changing what a
    record's identity *is* meant moving three sites together.
    """
    return is_adr_path(old) and is_adr_path(new) and adr_number(old) == adr_number(new)


def adr_number(path: str) -> str:
    match = ADR_PATH.match(path)
    if not match:
        raise CannotAnswer(f"{path} is not an ADR path")
    return match.group(1)


def status_of(text: str, path: str) -> str:
    """The ADR's Status. Missing *or repeated* is a fault, never a silent answer.

    A one-character edit to the header — `- Status:` for `- **Status:**` — would
    otherwise disable the gate for that file, permanently and silently.

    A *second* line is the same hole from the other side, and exactly the one
    `frozen_spans` refuses for repeated headings. Returning the first match let a
    line hidden in an HTML comment — not a code fence, and how every ADR opens —
    answer for the visible header.
    """
    found = [
        match.group(1)
        for line in lines_of(visible_prose(text, path))
        if (match := STATUS_LINE.match(line))
    ]
    if not found:
        raise CannotAnswer(
            f"{path}: no `**Status:**` line; the gate cannot tell whether it is frozen"
        )
    if len(found) > 1:
        raise CannotAnswer(
            f"{path}: has {len(found)} `**Status:**` lines ({', '.join(found)}); "
            "the gate cannot tell which one is the status"
        )
    return found[0]


def headings_of(text: str, path: str = "") -> list[tuple[str, int]]:
    """Each `## Heading` with its line number.

    One statement of "where the headings are", because `sections_of` and
    `frozen_spans` both need it and two copies of the same regex walk have to
    agree about fences, indentation and trailing whitespace forever.
    """
    return [
        (found.group(1), number)
        for number, line in enumerate(lines_of(visible_prose(text, path)), start=1)
        if (found := SECTION_HEADING.match(line))
    ]


def sections_of(text: str, path: str = "") -> dict[str, tuple[int, int]]:
    """Each `## Heading` mapped to the line range it owns, 1-based inclusive."""
    headings = headings_of(text, path)
    total = len(lines_of(visible_prose(text, path)))
    spans: dict[str, tuple[int, int]] = {}
    for index, (name, start) in enumerate(headings):
        end = headings[index + 1][1] - 1 if index + 1 < len(headings) else total
        spans.setdefault(name, (start, end))
    return spans


def frozen_spans(text: str, path: str) -> dict[str, tuple[int, int]]:
    """The frozen sections' line ranges. Missing or repeated is a fault.

    ADR-0001 requires every ADR to carry both. One that does not is the "cannot
    answer" case, a level down from an unreadable Status: an absent section
    touches nothing, so renaming `## Decision` would unfreeze it silently.

    A *repeated* heading is the same class arriving from the other side.
    `sections_of` keeps the first span, so everything under a second
    `## Decision` belongs to no span at all and is editable — the file would
    read as having a decision the gate does not guard.
    """
    spans = sections_of(text, path)
    missing = [name for name in FROZEN_SECTIONS if name not in spans]
    if missing:
        raise CannotAnswer(
            f"{path}: has no {' or '.join(missing)} section; "
            "the gate cannot tell which lines are frozen"
        )
    names = [name for name, _ in headings_of(text, path)]
    repeated = [name for name in FROZEN_SECTIONS if names.count(name) > 1]
    if repeated:
        raise CannotAnswer(
            f"{path}: has {' and '.join(repeated)} more than once; "
            "the gate cannot tell which one is frozen"
        )
    return {name: spans[name] for name in FROZEN_SECTIONS}


def touches(spans: dict[str, tuple[int, int]], name: str, lines: set[int]) -> bool:
    if name not in spans:
        return False
    start, end = spans[name]
    return bool(lines & set(range(start, end + 1)))


def declared_superseded(text: str, path: str = "") -> set[str]:
    """Every ADR number this record *declares* it supersedes."""
    body = visible_prose(text, path)
    declared: set[str] = set()
    for found in SUPERSEDES_FIELD.finditer(body):
        rest = body[found.end() :]
        stop = CLAUSE_END.search(rest)
        declared.update(ADR_REFERENCE.findall(rest[: stop.start()] if stop else rest))
    return declared


def names_as_superseded(text: str, number: str, path: str = "") -> bool:
    """Whether `text` declares that it supersedes ADR-`number`."""
    return number in declared_superseded(text, path)
