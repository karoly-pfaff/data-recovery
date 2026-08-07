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

**Only Decision and Consequences are frozen.** Status must change — that is how
superseding is recorded. Dates, a typo in Context, a corrected link: none of
those is the decision. Freezing the whole file would make the rule unusable and
would train people to bypass the gate, which is worse than not having one.

**Two escapes, and both must name the ADR being edited:**

1. the same diff adds a new ADR that names it as superseded, or
2. the same diff marks that ADR `Superseded`.

"Any new ADR in the diff" is not enough. The loose reading would let a change
that legitimately adds one record quietly rewrite an unrelated one — a superset
of the breach this exists to catch.

**What it cannot do**, stated because a gate that overstates itself is this
milestone's subject: it catches an *edit*, not an *inaccuracy*. An ADR that was
wrong the day it was written passes forever. Accuracy is the milestone audit's
job (`docs/code-quality.md`).
"""
from __future__ import annotations

import argparse
import logging
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

# The sections an Accepted ADR may not have rewritten under it.
FROZEN_SECTIONS = ("Decision", "Consequences")

ADR_PATH = re.compile(r"^docs/architecture/adr/adr-(\d{4})-[a-z0-9-]+\.md$")
# `a..b` and `a...b` both end at `b`. Splitting on the literal ".." turns
# "main...HEAD" — this gate's own default — into ".HEAD", which names nothing.
RANGE_SEPARATOR = re.compile(r"\.{2,3}")
SECTION_HEADING = re.compile(r"^##\s+(.+?)\s*$")
STATUS_LINE = re.compile(r"^[-*]?\s*\*\*Status:?\*\*[:\s]*([A-Za-z]+)")
# A supersession is *declared*, in the header field, not mentioned in passing.
# Prose is not a declaration: ADR-0012's Context says "an Accepted ADR is
# superseded by a new record, not edited" two sentences from "ADR-0005", and
# reading any nearby "supersedes" as a claim excused exactly the edit this gate
# was written to refuse. The header is the one place a record states what it
# replaces, and the one place a reader looks.
SUPERSEDES_FIELD = re.compile(r"^\s*[-*]?\s*\*\*Supersedes:?\*\*", re.IGNORECASE | re.MULTILINE)
ADR_REFERENCE = re.compile(r"ADR-(\d{4})")
# Where a "supersedes" clause stops. Not a character count: ADR-0012's header is
#
#     - **Supersedes:** the *Validated* half of
#       [ADR-0011](…)
#     - **Implements:** [ADR-0005](…)
#
# so the clause wraps — a same-line search misses ADR-0011 — while a fixed
# window of a couple of hundred characters swallows the *next* bullet and would
# excuse an edit to ADR-0005 on the strength of a sentence about ADR-0011. Both
# were written before this comment. The clause ends at the next bullet or the
# next blank line, whichever comes first.
CLAUSE_END = re.compile(r"\n\s*[-*]\s|\n\s*\n")


@dataclass(frozen=True)
class Breach:
    """One frozen section of one Accepted ADR, changed with no trace."""

    adr: str
    section: str

    def __str__(self) -> str:
        return f"{self.adr}: {self.section} was edited while the ADR is Accepted"


def run_git(args: list[str]) -> str:
    """`git` output, or a fatal exit. A gate that cannot read its input has not
    inspected anything, and must not report a pass."""
    finished = subprocess.run(
        ["git", *args], capture_output=True, text=True, check=False, encoding="utf-8"
    )
    if finished.returncode != 0:
        logging.error("adr gate: git %s failed: %s", " ".join(args), finished.stderr.strip())
        raise SystemExit(2)
    return finished.stdout


def changed_adrs(diff_range: str) -> list[str]:
    """The ADR files the range *modifies*, as repository-relative paths.

    Added files are excluded: every line of a new file is "changed", so a new
    ADR would otherwise report its own Decision and Consequences as edited —
    which is the one thing this gate exists to permit.
    """
    listed = run_git(
        ["diff", "--name-only", "--diff-filter=M", diff_range, "--", "docs/architecture/adr/"]
    )
    return [line for line in listed.splitlines() if ADR_PATH.match(line)]


def added_adrs(diff_range: str) -> list[str]:
    listed = run_git(
        ["diff", "--name-only", "--diff-filter=A", diff_range, "--", "docs/architecture/adr/"]
    )
    return [line for line in listed.splitlines() if ADR_PATH.match(line)]


def range_end(diff_range: str) -> str:
    """The commit a range ends at; an empty right-hand side means `HEAD`, which
    is what git itself assumes."""
    return RANGE_SEPARATOR.split(diff_range)[-1].strip() or "HEAD"


def adr_number(path: str) -> str:
    match = ADR_PATH.match(path)
    return match.group(1) if match else ""


def status_of(text: str) -> str:
    for line in text.splitlines():
        found = STATUS_LINE.match(line.strip())
        if found:
            return found.group(1)
    return ""


def sections_of(text: str) -> dict[str, tuple[int, int]]:
    """Each `## Heading` mapped to the line range it owns, 1-based inclusive."""
    headings: list[tuple[str, int]] = []
    for number, line in enumerate(text.splitlines(), start=1):
        found = SECTION_HEADING.match(line)
        if found:
            headings.append((found.group(1), number))

    spans: dict[str, tuple[int, int]] = {}
    total = len(text.splitlines())
    for index, (name, start) in enumerate(headings):
        end = headings[index + 1][1] - 1 if index + 1 < len(headings) else total
        spans[name] = (start, end)
    return spans


def changed_lines(diff_range: str, path: str) -> set[int]:
    """Line numbers touched on the *new* side of the diff for one file."""
    patch = run_git(["diff", "--unified=0", diff_range, "--", path])
    touched: set[int] = set()
    for line in patch.splitlines():
        header = re.match(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", line)
        if header:
            start = int(header.group(1))
            count = int(header.group(2) or 1)
            touched.update(range(start, start + count))
    return touched


def superseded_by_the_same_change(diff_range: str, path: str, number: str) -> bool:
    """Whether this change leaves a trace pointing at *this* ADR.

    Either a new ADR in the same diff names it as superseded, or its own Status
    became `Superseded` in the same diff. Both leave the record a reader can
    follow; neither is satisfied by an unrelated ADR merely existing.
    """
    for added in added_adrs(diff_range):
        text = run_git(["show", f"{range_end(diff_range)}:{added}"])
        if names_as_superseded(text, number):
            return True
    after = run_git(["show", f"{range_end(diff_range)}:{path}"])
    return status_of(after).lower() == "superseded"


def names_as_superseded(text: str, number: str) -> bool:
    """Whether `text` says it supersedes ADR-`number`.

    Only the `**Supersedes:**` header field counts, and only to the end of its
    own clause — the next bullet or the next blank line. The field may wrap onto
    a continuation line, so the reference need not be on the same line; it must
    not be borrowed from the bullet below, nor from prose elsewhere in the file.
    """
    for found in SUPERSEDES_FIELD.finditer(text):
        rest = text[found.end() :]
        stop = CLAUSE_END.search(rest)
        clause = rest[: stop.start()] if stop else rest
        if number in ADR_REFERENCE.findall(clause):
            return True
    return False


def breaches_in(diff_range: str, path: str) -> list[Breach]:
    after = run_git(["show", f"{range_end(diff_range)}:{path}"])
    if status_of(after).lower() != "accepted":
        return []

    spans = sections_of(after)
    touched = changed_lines(diff_range, path)
    number = adr_number(path)
    hit = [
        Breach(f"ADR-{number}", name)
        for name in FROZEN_SECTIONS
        if name in spans and touched & set(range(spans[name][0], spans[name][1] + 1))
    ]
    if hit and superseded_by_the_same_change(diff_range, path, number):
        return []
    return hit


def main() -> int:
    parser = argparse.ArgumentParser(description="Refuse an in-place edit to an Accepted ADR.")
    parser.add_argument("range", nargs="?", default="main...HEAD")
    args = parser.parse_args()

    logging.basicConfig(format="%(message)s", stream=sys.stderr)
    if int(run_git(["rev-list", "--count", args.range]).strip() or 0) == 0:
        logging.error("adr gate: %s names no commits; refusing to pass an empty gate", args.range)
        return 2

    breaches = [b for path in changed_adrs(args.range) for b in breaches_in(args.range, path)]
    for breach in breaches:
        logging.error("adr gate: %s", breach)
    if breaches:
        logging.error(
            "adr gate: an Accepted ADR is superseded by a new record, not edited (ADR-0001)."
        )
        return 1
    print(f"adr gate: no Accepted ADR was edited in place over {args.range}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
