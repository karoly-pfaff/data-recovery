#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What one ADR is, and what it says about itself.

Split out of `check_adr_immutability.py` (story-0705), together with
`adr_range.py`, when the one file grew past the 250-line limit while its escape
clause was hardened four times. The division is by subject, bottom up: this
module knows one *record* — its number, its status, its sections, what it
declares superseded — and nothing about git; `adr_range` knows what a git
*range* changed and nothing about markdown; the gate applies the *rule* to both.

Everything here parses **outside fenced code blocks**. A `## Heading` or a
`**Supersedes:**` inside a fence is an example, and an example is not a
declaration — the same distinction that keeps prose from excusing an edit. An
ADR documenting the ADR process would otherwise illustrate the header and
excuse whatever it named.
"""
from __future__ import annotations

import re

# The sections an Accepted ADR may not have rewritten under it. Status is not
# among them: it has to change for a supersession to be recordable at all.
FROZEN_SECTIONS = ("Decision", "Consequences")

ADR_PATH = re.compile(r"^docs/architecture/adr/adr-(\d{4})-[a-z0-9-]+\.md$")

SECTION_HEADING = re.compile(r"^##\s+(.+?)\s*$")
STATUS_LINE = re.compile(r"^[-*]?\s*\*\*Status:?\*\*[:\s]*([A-Za-z]+)")

# A supersession is *declared*, in the header field, not mentioned in passing.
# ADR-0012's Context says "an Accepted ADR is superseded by a new record, not
# edited" two sentences from "ADR-0005", and reading any nearby mention as a
# claim excused precisely the edit this gate refuses.
SUPERSEDES_FIELD = re.compile(
    r"^\s*[-*]?\s*\*\*Supersedes:?\*\*", re.IGNORECASE | re.MULTILINE
)
ADR_REFERENCE = re.compile(r"ADR-(\d{4})")

# Where a `**Supersedes:**` clause stops: the next *top-level* bullet or a blank
# line. Not a character count — a fixed window swallows the following bullet and
# would excuse an edit to the ADR named there. Indented bullets continue the
# clause, so the natural multi-ADR form is a nested list rather than a refusal.
CLAUSE_END = re.compile(r"\n(?=[-*]\s)|\n\s*\n")

CODE_FENCE = re.compile(r"^\s*(?:```|~~~)")


class CannotAnswer(Exception):
    """The question cannot be answered from what is here. Never a pass.

    Named for the condition rather than for the caller: a malformed document
    and a malformed range are the same kind of event, and both must exit 2.
    """


def adr_number(path: str) -> str:
    match = ADR_PATH.match(path)
    if not match:
        raise CannotAnswer(f"{path} is not an ADR path")
    return match.group(1)


def outside_fences(text: str, path: str = "") -> str:
    """The same text with fenced blocks blanked, line count preserved.

    Blanked rather than removed so line numbers still line up with a diff.

    An *unbalanced* fence is a fault, not a blanked tail. Toggling on every
    fence line with no balance check meant one unclosed ``` blanked the rest of
    the file: `sections_of` then found no Decision, and the Decision could be
    rewritten freely. That is the same silent pass an unreadable Status used to
    be, reintroduced by the fix for fenced examples.
    """
    kept: list[str] = []
    inside = False
    for line in text.splitlines():
        if CODE_FENCE.match(line):
            inside = not inside
            kept.append("")
            continue
        kept.append("" if inside else line)
    if inside:
        raise CannotAnswer(
            f"{path or 'document'}: a code fence is opened and never closed; "
            "the gate cannot tell which lines are prose"
        )
    return "\n".join(kept)


def status_of(text: str, path: str) -> str:
    """The ADR's Status. Unreadable is a fault, not a silent 'not Accepted'.

    A one-character edit to the header — `- Status:` for `- **Status:**` —
    would otherwise disable the gate for that file, permanently and silently.
    """
    for line in outside_fences(text, path).splitlines():
        found = STATUS_LINE.match(line.strip())
        if found:
            return found.group(1)
    raise CannotAnswer(
        f"{path}: no `**Status:**` line; the gate cannot tell whether it is frozen"
    )


def sections_of(text: str, path: str = "") -> dict[str, tuple[int, int]]:
    """Each `## Heading` mapped to the line range it owns, 1-based inclusive."""
    lines = outside_fences(text, path).splitlines()
    headings = [
        (found.group(1), number)
        for number, line in enumerate(lines, start=1)
        if (found := SECTION_HEADING.match(line))
    ]
    spans: dict[str, tuple[int, int]] = {}
    for index, (name, start) in enumerate(headings):
        end = headings[index + 1][1] - 1 if index + 1 < len(headings) else len(lines)
        spans.setdefault(name, (start, end))
    return spans


def frozen_spans(text: str, path: str) -> dict[str, tuple[int, int]]:
    """The frozen sections' line ranges. A missing one is a fault.

    ADR-0001 requires every ADR to carry both. One that does not is the "cannot
    answer" case, a level down from an unreadable Status: an absent section
    touches nothing, so renaming `## Decision` would unfreeze it silently.
    """
    spans = sections_of(text, path)
    missing = [name for name in FROZEN_SECTIONS if name not in spans]
    if missing:
        raise CannotAnswer(
            f"{path}: has no {' or '.join(missing)} section; "
            "the gate cannot tell which lines are frozen"
        )
    return {name: spans[name] for name in FROZEN_SECTIONS}


def touches(spans: dict[str, tuple[int, int]], name: str, lines: set[int]) -> bool:
    if name not in spans:
        return False
    start, end = spans[name]
    return bool(lines & set(range(start, end + 1)))


def names_as_superseded(text: str, number: str) -> bool:
    """Whether `text` *declares* that it supersedes ADR-`number`."""
    body = outside_fences(text, "")
    for found in SUPERSEDES_FIELD.finditer(body):
        rest = body[found.end() :]
        stop = CLAUSE_END.search(rest)
        clause = rest[: stop.start()] if stop else rest
        if number in ADR_REFERENCE.findall(clause):
            return True
    return False
