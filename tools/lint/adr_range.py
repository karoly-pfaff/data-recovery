#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a git range changed, as the ADR gate needs to see it.

The git half of story-0705's split; `adr_document` is the markdown half and
`check_adr_immutability` is the rule that uses both. Nothing here reads an ADR:
it answers which files a range touched, which lines it touched in them, and what
a file said at either end.

Two of those are subtler than they look, and both were silent passes first:

- **Where a range starts.** `a...b` measures its old side from the merge base,
  not from `a`. Three dots is the gate's own default and what CI passes.
- **Which lines it touched.** On *both* sides. A pure removal gains no line on
  the new side, so a new-side-only reading records the edit as untouched.
"""
from __future__ import annotations

import re
import subprocess

from adr_document import ADR_PATH, CannotAnswer

ADR_DIRECTORY = "docs/architecture/adr/"

# `a..b` and `a...b` both end at `b`, but they start somewhere different, and
# the separator is what says where. Splitting on the literal ".." also turns
# "main...HEAD" — the gate's own default — into ".HEAD", which names nothing.
RANGE_SEPARATOR = re.compile(r"(\.{2,3})")

HUNK_HEADER = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


def run_git(args: list[str]) -> str:
    finished = subprocess.run(
        ["git", *args], capture_output=True, text=True, check=False, encoding="utf-8"
    )
    if finished.returncode != 0:
        raise CannotAnswer(f"git {' '.join(args)} failed: {finished.stderr.strip()}")
    return finished.stdout


def split_range(diff_range: str) -> tuple[str, str, str]:
    """A range as `git` reads it: left side, separator, right side."""
    parts = RANGE_SEPARATOR.split(diff_range)
    if len(parts) != 3:
        raise CannotAnswer(
            f"{diff_range!r} is not a range: it names one commit, and "
            "`git diff <commit>` compares the working tree rather than two commits"
        )
    left, separator, right = parts
    return left.strip() or "HEAD", separator, right.strip() or "HEAD"


def range_end(diff_range: str) -> str:
    return split_range(diff_range)[2]


def range_start(diff_range: str) -> str:
    """The commit the diff's *old* side is measured from.

    For `a...b` that is the merge base, which is what `git diff` itself uses —
    not `a`. Reading the pre-image from `a` instead works only while the two
    coincide, and silently judges the old-side line numbers against a file that
    existed on neither side once `a` moves ahead. Three dots is this gate's own
    default and what CI passes, so that is the normal case, not an edge one.
    """
    left, separator, right = split_range(diff_range)
    if separator == "...":
        return run_git(["merge-base", left, right]).strip()
    return left


def commit_count(diff_range: str) -> int:
    return int(run_git(["rev-list", "--count", diff_range]).strip() or 0)


def text_at(commit: str, path: str) -> str:
    return run_git(["show", f"{commit}:{path}"])


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


def changed_lines(diff_range: str, path: str) -> tuple[set[int], set[int]]:
    """Lines touched, on the old side and on the new side.

    Both are needed. A pure removal is `@@ -18 +17,0 @@`: the new side gains no
    line, so a new-side-only reading records the edit as untouched. The removed
    content belonged to a section of the *old* file, so deletions are judged
    against the pre-image. Deleting a consequence you no longer like is at least
    as much a breach as adding one, and quieter.
    """
    # Both names, so rename detection can pair them. With only the new path in
    # the pathspec git cannot see the old one, renders the file as freshly
    # added, and reports every line as touched — which turns a slug correction
    # into a demand that the ADR be superseded.
    was = previous_path(diff_range, path)
    pathspec = [path] if was == path else [was, path]
    patch = run_git(["diff", "--unified=0", "-M", diff_range, "--", *pathspec])
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
