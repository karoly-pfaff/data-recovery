#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a git range changed, as the ADR gate needs to see it.

The range layer of story-0705's split: built on `adr_document`'s vocabulary,
and used by `check_adr_immutability`. Nothing here reads an ADR's prose. It
answers which records a range touched, what each was called before, which lines
it touched in them, and what a file said at either end.

Four things here are subtler than they look, and every one of them was a silent
pass on `4a4221e` — the single commit this gate exists to catch:

- **Where the gate runs from.** The pathspec is relative to the working
  directory, so from `docs/` it matched nothing, the range was still non-empty,
  and the gate reported "no Accepted ADR was edited" and exited 0. Every command
  runs from the top level now.
- **What counts as text.** One `.gitattributes` line marking the ADRs `-diff`
  makes `git diff` print "Binary files differ" and no `@@` headers at all, so
  every hunk set comes back empty. `--text` is not optional here.
- **Where a range starts.** `a...b` measures its old side from the merge base,
  not from `a`. Three dots is the gate's own default and what CI passes.
- **Whether it is empty.** `rev-list --count a...b` counts the symmetric
  difference, which is not what `git diff a...b` reads.

**Flags, not config pins.** An earlier version pinned `diff.external`,
`diff.renames` and `core.quotePath` with `-c`. Every one was redundant with a
flag already on the command line — removing them left the whole suite green,
which is the check that should have been run when they were written. What is
load-bearing is below, and each flag has a test that fails without it.
"""
from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from functools import lru_cache

from adr_document import ADR_DIRECTORY, CannotAnswer

# `--no-ext-diff` beats `diff.external`, `--text` beats a `-diff` attribute,
# `--no-textconv` beats a textconv filter that would renumber the lines, and
# `-M` beats `diff.renames=false`. A command-line flag outranks configuration,
# which is why these are flags.
DIFF_FLAGS = ("--no-ext-diff", "--no-textconv", "--text", "-M")

# `a..b` and `a...b` both end at `b`, but they start somewhere different, and
# the separator is what says where. Splitting on the literal ".." also turns
# "main...HEAD" — the gate's own default — into ".HEAD", which names nothing.
RANGE_SEPARATOR = re.compile(r"(\.{2,3})")

HUNK_HEADER = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


@dataclass(frozen=True)
class Change:
    """One file's fate over a range: what it was called, and what it now is.

    `old` is empty for an addition and `new` for a deletion. A rename carries
    both, which is the only way to see that a record left the ADR naming
    convention, or changed its number, rather than simply vanishing.
    """

    kind: str
    old: str
    new: str

    @property
    def deleted(self) -> bool:
        return self.kind.startswith("D")


@lru_cache(maxsize=1)
def top_level() -> str:
    """The repository root, which every command below runs from.

    Not the caller's working directory. `ADR_DIRECTORY` is a relative pathspec,
    so running the gate from anywhere but the root matched no files at all —
    and a gate that inspects nothing and exits 0 is this milestone's subject.
    """
    finished = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True,
        check=False, encoding="utf-8",
    )
    if finished.returncode != 0:
        raise CannotAnswer(f"not inside a git repository: {finished.stderr.strip()}")
    return finished.stdout.strip()


def run_git(args: list[str]) -> str:
    finished = subprocess.run(
        ["git", *args], cwd=top_level(), capture_output=True, text=True, check=False,
        encoding="utf-8",
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
    """How many commits the *diff* spans — not the symmetric difference.

    `rev-list --count a...b` counts commits unique to either side, while
    `git diff a...b` reads `merge-base(a,b)..b`. They disagree exactly when the
    guard matters: `HEAD...HEAD~3` counts three and diffs nothing, so a swapped
    or stale range expression was reported as a clean pass over an empty diff.
    """
    return int(
        run_git(["rev-list", "--count", f"{range_start(diff_range)}..{range_end(diff_range)}"])
        .strip()
        or 0
    )


def text_at(commit: str, path: str) -> str:
    return run_git(["show", f"{commit}:{path}"])


def changes_in(diff_range: str) -> list[Change]:
    """Every file the range touched under the ADR directory, with its old name.

    Filtered by directory rather than by the ADR path pattern, and *not* by the
    new name: a record moved to `superseded/adr-0005-….md` no longer matches the
    pattern, so filtering on the new name erased it from the gate entirely while
    git reported it as a rename rather than a deletion.
    """
    listed = run_git(
        ["diff", *DIFF_FLAGS, "--name-status", diff_range, "--", ADR_DIRECTORY]
    )
    changes: list[Change] = []
    for line in listed.splitlines():
        fields = line.split("\t")
        if len(fields) == 3:
            changes.append(Change(kind=fields[0], old=fields[1], new=fields[2]))
        elif len(fields) == 2:
            kind, path = fields
            changes.append(
                Change(
                    kind=kind,
                    old="" if kind.startswith("A") else path,
                    new="" if kind.startswith("D") else path,
                )
            )
        else:
            # In a gate whose thesis is that unreadable input is a fault, a
            # diff line it cannot parse must not be skipped in silence.
            raise CannotAnswer(f"cannot read this line of git's output: {line!r}")
    return changes


def changed_lines(diff_range: str, old: str, new: str) -> tuple[set[int], set[int]]:
    """Lines touched, on the old side and on the new side.

    Both are needed. A pure removal is `@@ -18 +17,0 @@`: the new side gains no
    line, so a new-side-only reading records the edit as untouched. The removed
    content belonged to a section of the *old* file, so deletions are judged
    against the pre-image. Deleting a consequence you no longer like is at least
    as much a breach as adding one, and quieter.

    Both names are passed so rename detection can pair them. With only the new
    path in the pathspec git cannot see the old one, renders the file as freshly
    added, and reports every line as touched — which turns a slug correction
    into a demand that the ADR be superseded.
    """
    pathspec = sorted({p for p in (old, new) if p})
    patch = run_git(["diff", *DIFF_FLAGS, "--unified=0", diff_range, "--", *pathspec])
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
