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

This module reads *a range*. What an ADR says about itself — its status, its
sections, what it declares superseded — is `adr_document`. The rule, its two
escapes and their limits are stated once, in
`docs/testing/quality-gates.md`; the short version:

- only **Decision** and **Consequences** are frozen, because Status must change
  for a supersession to be recordable at all;
- an edit is excused only by a change that *names* the ADR — a new record
  declaring `**Supersedes:** ADR-NNNN`, or its own Status becoming `Superseded`;
- anything unreadable is a fault (exit 2), never a pass;
- it catches an *edit*, not an *inaccuracy*.
"""
from __future__ import annotations

import argparse
import logging
import re
import subprocess
import sys
from dataclasses import dataclass

from adr_document import (
    FROZEN_SECTIONS,
    GateFault,
    names_as_superseded,
    sections_of,
    status_of,
    touches,
)

ADR_DIRECTORY = "docs/architecture/adr/"
ADR_PATH = re.compile(r"^docs/architecture/adr/adr-(\d{4})-[a-z0-9-]+\.md$")

# `a..b` and `a...b` both end at `b`. Splitting on the literal ".." turns
# "main...HEAD" — this gate's own default — into ".HEAD", which names nothing.
RANGE_SEPARATOR = re.compile(r"\.{2,3}")

HUNK_HEADER = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


@dataclass(frozen=True)
class Breach:
    """One frozen section of one Accepted ADR, changed with no trace."""

    adr: str
    section: str

    def __str__(self) -> str:
        return f"{self.adr}: {self.section} was edited while the ADR is Accepted"


def run_git(args: list[str]) -> str:
    finished = subprocess.run(
        ["git", *args], capture_output=True, text=True, check=False, encoding="utf-8"
    )
    if finished.returncode != 0:
        raise GateFault(f"git {' '.join(args)} failed: {finished.stderr.strip()}")
    return finished.stdout


def range_end(diff_range: str) -> str:
    """The commit a range ends at; an empty right-hand side means `HEAD`."""
    if not RANGE_SEPARATOR.search(diff_range):
        raise GateFault(
            f"{diff_range!r} is not a range: it names one commit, and "
            "`git diff <commit>` compares the working tree rather than two commits"
        )
    return RANGE_SEPARATOR.split(diff_range)[-1].strip() or "HEAD"


def range_start(diff_range: str) -> str:
    return (RANGE_SEPARATOR.split(diff_range)[0] or "HEAD") + "^0"


def adr_paths(diff_range: str, filters: str) -> list[str]:
    listed = run_git(
        ["diff", "--name-only", f"--diff-filter={filters}", diff_range, "--", ADR_DIRECTORY]
    )
    return [line for line in listed.splitlines() if ADR_PATH.match(line)]


def previous_path(diff_range: str, path: str) -> str:
    """What this file was called before the change.

    A rename is reported as `R<score>\told\tnew`, and asking for the *new* path
    in the old commit fails outright.
    """
    for line in run_git(
        ["diff", "--name-status", "-M", diff_range, "--", ADR_DIRECTORY]
    ).splitlines():
        fields = line.split("\t")
        if fields[0].startswith("R") and len(fields) == 3 and fields[2] == path:
            return fields[1]
    return path


def adr_number(path: str) -> str:
    match = ADR_PATH.match(path)
    if not match:
        raise GateFault(f"{path} is not an ADR path")
    return match.group(1)


def changed_lines(diff_range: str, path: str) -> tuple[set[int], set[int]]:
    """Lines touched, on the old side and on the new side.

    Both are needed. A pure removal is `@@ -18 +17,0 @@`: the new side gains no
    line, so a new-side-only reading records the edit as untouched and the gate
    passes. The removed content belonged to a section of the *old* file, so
    deletions are judged against the pre-image. Deleting a consequence you no
    longer like is at least as much a breach as adding one, and quieter.
    """
    patch = run_git(["diff", "--unified=0", diff_range, "--", path])
    before: set[int] = set()
    after: set[int] = set()
    for line in patch.splitlines():
        header = HUNK_HEADER.match(line)
        if not header:
            continue
        old_start, old_count, new_start, new_count = (
            int(header.group(1)),
            int(header.group(2)) if header.group(2) is not None else 1,
            int(header.group(3)),
            int(header.group(4)) if header.group(4) is not None else 1,
        )
        before.update(range(old_start, old_start + old_count))
        after.update(range(new_start, new_start + new_count))
    return before, after


def superseded_by_the_same_change(diff_range: str, path: str, number: str) -> bool:
    for added in adr_paths(diff_range, "A"):
        if names_as_superseded(run_git(["show", f"{range_end(diff_range)}:{added}"]), number):
            return True
    after = run_git(["show", f"{range_end(diff_range)}:{path}"])
    return status_of(after, path).lower() == "superseded"


def frozen_sections_touched(diff_range: str, path: str) -> list[str]:
    """Which frozen sections this change disturbed, judged on both sides.

    The old file decides for removals and the new one for everything else, so a
    section rewritten, added to, or emptied all report the same way.
    """
    new_text = run_git(["show", f"{range_end(diff_range)}:{path}"])
    if status_of(new_text, path).lower() != "accepted":
        return []

    old_text = run_git(
        ["show", f"{range_start(diff_range)}:{previous_path(diff_range, path)}"]
    )
    old_lines, new_lines = changed_lines(diff_range, path)
    old_spans, new_spans = sections_of(old_text), sections_of(new_text)
    return [
        name
        for name in FROZEN_SECTIONS
        if touches(new_spans, name, new_lines) or touches(old_spans, name, old_lines)
    ]


def breaches_in(diff_range: str, path: str) -> list[Breach]:
    sections = frozen_sections_touched(diff_range, path)
    number = adr_number(path)
    if not sections or superseded_by_the_same_change(diff_range, path, number):
        return []
    return [Breach(f"ADR-{number}", name) for name in sections]


def deleted_adrs(diff_range: str) -> list[str]:
    """An Accepted ADR removed outright is the same breach, more thoroughly."""
    return [
        path
        for path in adr_paths(diff_range, "D")
        if status_of(run_git(["show", f"{range_start(diff_range)}:{path}"]), path).lower()
        == "accepted"
    ]


def report(diff_range: str) -> list[str]:
    """Every complaint this range earns, as lines ready to print."""
    # M and R both mean "this file was edited": a rename with an edit is
    # reported as R, and filtering to M alone let it through untouched.
    complaints = [
        str(breach)
        for path in adr_paths(diff_range, "MR")
        for breach in breaches_in(diff_range, path)
    ]
    return complaints + [
        f"{path} was deleted while Accepted" for path in deleted_adrs(diff_range)
    ]


def verdict(diff_range: str) -> tuple[int, list[str]]:
    """The exit code and what to say, with no I/O of its own."""
    range_end(diff_range)
    if int(run_git(["rev-list", "--count", diff_range]).strip() or 0) == 0:
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
    except GateFault as fault:
        logging.error("adr gate: %s", fault)
        return 2

    for complaint in complaints:
        logging.error("adr gate: %s", complaint)
    if code == 0:
        print(f"adr gate: no Accepted ADR was edited in place over {args.range}")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
