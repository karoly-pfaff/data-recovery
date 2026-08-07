#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What one ADR is, and what it says about itself.

Split out of `check_adr_immutability.py` (story-0705), together with
`adr_range.py`, when the one file grew past the 250-line limit while its escape
clause was hardened four times. The three are a **stack, not two disjoint
halves**: this module is the base — the vocabulary of what an ADR *is* (where
they live, what a path must look like, what one record says about itself) plus
the fault type that says the question cannot be answered. `adr_range` is built
on that vocabulary and adds what a git range changed; the gate is the rule that
uses both. An earlier docstring here claimed a clean text/git divide, which was
not true of `ADR_PATH` or of `CannotAnswer` and made the seam read as leaky when
it was only mis-described.

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

# Stated once. `adr_range` needs the directory as a pathspec and the gate needs
# the pattern; spelling the path in both is one fact in two places, and moving
# the ADRs would then need both changed.
ADR_DIRECTORY = "docs/architecture/adr/"
ADR_PATH = re.compile(rf"^{re.escape(ADR_DIRECTORY)}adr-(\d{{4}})-[a-z0-9-]+\.md$")


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

# The delimiter is captured so a closing fence can be matched to *its* opener.
# A boolean toggled by any fence line cannot nest: an ADR illustrating the ADR
# template with a ````-fenced example containing ``` blocks would un-blank the
# inner text, and a `**Supersedes:**` in the example would read as a
# declaration — the exact defect fencing was introduced to close.
CODE_FENCE = re.compile(r"^\s*(`{3,}|~{3,})")

# A record leaves draft when it is Accepted and stays on the record once it is
# Superseded — surviving to be read is the entire point of superseding rather
# than deleting. Both are immutable; only a draft may still be reshaped.
ON_THE_RECORD = ("accepted", "superseded")


def is_on_the_record(status: str) -> bool:
    return status.lower() in ON_THE_RECORD


class CannotAnswer(Exception):
    """The question cannot be answered from what is here. Never a pass.

    Named for the condition rather than for the caller: a malformed document
    and a malformed range are the same kind of event, and both must exit 2.
    """


def lines_of(text: str) -> list[str]:
    """The lines *git* sees: separated by `\\n`, and nothing else.

    `str.splitlines()` also breaks on U+2028, U+2029, U+0085, `\\v`, `\\f` and
    `\\x1c`-`\\x1e`. Git counts `\\n`. Every character of that class above a
    frozen heading shifted the computed span one line further down than the hunk
    headers say, so the top of the Decision fell outside its own section and
    could be rewritten with the gate green — no intent required, one form feed
    pasted from a word processor is enough. `docs/` is outside the encoding
    gate's roots, so nothing upstream rejects the character either.

    A trailing newline terminates the last line rather than starting a new one,
    which is also how git numbers it.
    """
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return lines


def is_adr_path(path: str) -> bool:
    return bool(path) and ADR_PATH.match(path) is not None


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

    Fences also **nest**, which a boolean cannot express. A block opened with
    four backticks is closed only by four or more of the same character, so the
    three-backtick blocks inside it stay fenced. Toggling on every fence line
    instead re-opened the outer block at the first inner one, un-blanking the
    example — and an example that reads as prose is the whole reason for
    blanking. An ADR documenting the ADR template is exactly that shape.
    """
    kept: list[str] = []
    opener: str | None = None
    for line in lines_of(text):
        found = CODE_FENCE.match(line)
        if found and opener is None:
            opener = found.group(1)
        elif found and found.group(1)[0] == opener[0] and len(found.group(1)) >= len(opener):
            opener = None
        elif opener is None:
            kept.append(line)
            continue
        kept.append("")
    if opener is not None:
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
    for line in lines_of(outside_fences(text, path)):
        found = STATUS_LINE.match(line.strip())
        if found:
            return found.group(1)
    raise CannotAnswer(
        f"{path}: no `**Status:**` line; the gate cannot tell whether it is frozen"
    )


def headings_of(text: str, path: str = "") -> list[tuple[str, int]]:
    """Each `## Heading` with its line number.

    One statement of "where the headings are", because `sections_of` and
    `frozen_spans` both need it and two copies of the same regex walk have to
    agree about fences, indentation and trailing whitespace forever.
    """
    return [
        (found.group(1), number)
        for number, line in enumerate(lines_of(outside_fences(text, path)), start=1)
        if (found := SECTION_HEADING.match(line))
    ]


def sections_of(text: str, path: str = "") -> dict[str, tuple[int, int]]:
    """Each `## Heading` mapped to the line range it owns, 1-based inclusive."""
    headings = headings_of(text, path)
    total = len(lines_of(outside_fences(text, path)))
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


def names_as_superseded(text: str, number: str, path: str = "") -> bool:
    """Whether `text` *declares* that it supersedes ADR-`number`."""
    body = outside_fences(text, path)
    for found in SUPERSEDES_FIELD.finditer(body):
        rest = body[found.end() :]
        stop = CLAUSE_END.search(rest)
        clause = rest[: stop.start()] if stop else rest
        if number in ADR_REFERENCE.findall(clause):
            return True
    return False
