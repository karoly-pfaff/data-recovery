#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What one ADR says about itself: its status, its sections, what it supersedes.

Split from `check_adr_immutability.py` (story-0705), which grew past the
250-line limit while its escape clause was hardened four times. The division is
by subject: this module reads *text* and knows nothing about git; the gate reads
*a range* and knows nothing about markdown.

Everything here parses **outside fenced code blocks**. A `## Heading` or a
`**Supersedes:**` inside a fence is an example, and an example is not a
declaration — the same distinction that keeps prose from excusing an edit. An
ADR documenting the ADR process would otherwise illustrate the header and
excuse whatever it named.
"""
from __future__ import annotations

import re

# The sections an Accepted ADR may not have rewritten under it.
FROZEN_SECTIONS = ("Decision", "Consequences")

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


class GateFault(Exception):
    """The gate cannot answer. Never a pass — always exit 2."""


def outside_fences(text: str) -> str:
    """The same text with fenced blocks blanked, line count preserved.

    Blanked rather than removed so line numbers still line up with a diff.
    """
    kept: list[str] = []
    inside = False
    for line in text.splitlines():
        if CODE_FENCE.match(line):
            inside = not inside
            kept.append("")
            continue
        kept.append("" if inside else line)
    return "\n".join(kept)


def status_of(text: str, path: str) -> str:
    """The ADR's Status. Unreadable is a fault, not a silent 'not Accepted'.

    A one-character edit to the header — `- Status:` for `- **Status:**` —
    would otherwise disable the gate for that file, permanently and silently.
    """
    for line in outside_fences(text).splitlines():
        found = STATUS_LINE.match(line.strip())
        if found:
            return found.group(1)
    raise GateFault(
        f"{path}: no `**Status:**` line; the gate cannot tell whether it is frozen"
    )


def sections_of(text: str) -> dict[str, tuple[int, int]]:
    """Each `## Heading` mapped to the line range it owns, 1-based inclusive."""
    lines = outside_fences(text).splitlines()
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


def touches(spans: dict[str, tuple[int, int]], name: str, lines: set[int]) -> bool:
    if name not in spans:
        return False
    start, end = spans[name]
    return bool(lines & set(range(start, end + 1)))


def names_as_superseded(text: str, number: str) -> bool:
    """Whether `text` *declares* that it supersedes ADR-`number`."""
    body = outside_fences(text)
    for found in SUPERSEDES_FIELD.finditer(body):
        rest = body[found.end() :]
        stop = CLAUSE_END.search(rest)
        clause = rest[: stop.start()] if stop else rest
        if number in ADR_REFERENCE.findall(clause):
            return True
    return False
