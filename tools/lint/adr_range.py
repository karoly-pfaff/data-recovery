#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a git range changed, as the ADR gate needs to see it.

The range layer of story-0705's split: built on `adr_document`'s vocabulary and
used by `check_adr_immutability`. Nothing here reads an ADR's prose. It answers
which records a range touched, what each was called before, which lines it
touched in them, and what a file said at either end.

Every question this module asks git is a place the answer can be quietly
emptied, and four of them were — each making the gate exit 0 on `4a4221e`, the
one commit it exists to catch. `docs/testing/quality-gates.md` lists them.
"""
from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from functools import lru_cache

from adr_document import ADR_DIRECTORY, CannotAnswer, is_adr_path

# Each defeats one way the repository or the reader can empty this gate's
# input; `quality-gates.md` tabulates which. Flags, because a command-line flag
# outranks configuration.
DIFF_FLAGS = ("--no-ext-diff", "--no-textconv", "--text", "-M")

# `a..b` and `a...b` both end at `b`, but they start somewhere different, and
# the separator is what says where. Splitting on the literal ".." also turns
# "main...HEAD" — the gate's own default — into ".HEAD", which names nothing.
RANGE_SEPARATOR = re.compile(r"(\.{2,3})")

HUNK_HEADER = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


@dataclass(frozen=True)
class Change:
    """One file's fate over a range: what it was called, and what it now is.

    `old` is empty for an addition and `new` for a deletion; a rename carries
    both, which is the only way to see a record leave the naming convention or
    change its number rather than simply vanish.
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

    Not the caller's working directory: `ADR_DIRECTORY` is a relative pathspec,
    so from anywhere else it matched no files at all.
    """
    # Bytes, for the reason `run_git` gives below.
    try:
        finished = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"], capture_output=True, check=False
        )
    except OSError as broken:
        raise CannotAnswer(f"could not run git: {broken}") from broken
    if finished.returncode != 0:
        raise CannotAnswer(
            f"not inside a git repository: "
            f"{finished.stderr.decode('utf-8', 'replace').strip()}"
        )
    return finished.stdout.decode("utf-8", "replace").strip()


def run_git(args: list[str], cwd: str | None = None) -> str:
    """Run git, turning every way it can fail into the one typed fault.

    Including the ways that are not a non-zero exit: no `git` on PATH, and a
    non-UTF-8 byte in an ADR (`docs/` is outside `check_encoding.py`'s roots, so
    nothing else catches that first). Both escaped as a traceback and exit 1 —
    the code `quality-gates.md` reserves for "found a violation".
    """
    try:
        finished = subprocess.run(
            ["git", *args], cwd=cwd or top_level(), capture_output=True, check=False
        )
    except OSError as broken:
        raise CannotAnswer(f"could not run git: {broken}") from broken
    if finished.returncode != 0:
        raise CannotAnswer(
            f"git {' '.join(args)} failed: "
            f"{finished.stderr.decode('utf-8', 'replace').strip()}"
        )
    # Decoded here rather than by `subprocess`, which does it on a reader thread
    # where the `UnicodeDecodeError` cannot be caught at this call: it surfaces
    # as `stdout is None` and dies further down as an `AttributeError`, exiting
    # 1 — the code that means "an ADR was edited".
    try:
        return finished.stdout.decode("utf-8")
    except UnicodeDecodeError as broken:
        raise CannotAnswer(
            f"git {' '.join(args)} returned bytes that are not UTF-8: {broken}"
        ) from broken


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
    not `a`. Reading it from `a` works only while the two coincide, and three
    dots is this gate's default and what CI passes, so a branch behind `main`
    is the normal case rather than an edge one.
    """
    left, separator, right = split_range(diff_range)
    if separator == "...":
        return run_git(["merge-base", left, right]).strip()
    return left


def commit_count(diff_range: str) -> int:
    """How many commits the *diff* spans — not the symmetric difference.

    `rev-list --count a...b` counts commits unique to either side; `git diff`
    reads `merge-base(a,b)..b`. `HEAD...HEAD~3` counts three and diffs nothing.
    """
    return int(
        run_git(["rev-list", "--count", f"{range_start(diff_range)}..{range_end(diff_range)}"])
        .strip()
        or 0
    )


def text_at(commit: str, path: str) -> str:
    return run_git(["show", f"{commit}:{path}"])


def parse_name_status(listed: str) -> list[Change]:
    """`--name-status` output as records. Pure, so it can be tested as one.

    Two fields is an add, delete or modify; three is a rename, carrying both
    names. Anything else the gate cannot read, and in a gate whose thesis is
    that unreadable input is a fault, it must not be skipped — a refusal git
    itself cannot provoke, so only a direct test can reach it.
    """
    changes: list[Change] = []
    for line in listed.splitlines():
        if not line.strip():
            continue
        fields = line.split("\t")
        # The status letter is what says which name is which; without it the
        # rest of the line cannot be assigned to a side.
        if not fields[0]:
            raise CannotAnswer(f"cannot read this line of git's output: {line!r}")
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
            raise CannotAnswer(f"cannot read this line of git's output: {line!r}")
    return changes


def changes_in(diff_range: str) -> list[Change]:
    """Every file the range touched under the ADR directory, with its old name.

    Filtered by directory rather than by the ADR path pattern, and *not* by the
    new name: a record moved to `superseded/adr-0005-….md` no longer matches the
    pattern, so filtering on the new name erased it from the gate entirely while
    git reported it as a rename rather than a deletion.
    """
    return parse_name_status(
        run_git(["diff", *DIFF_FLAGS, "--name-status", diff_range, "--", ADR_DIRECTORY])
    )


def adr_paths_at(commit: str) -> list[str]:
    """The ADR files that exist at this commit — by the gate's own definition.

    The gate's own root, checked the way `source_set.gate_files` checks a
    walking gate's: `git diff -- <pathspec>` is silent and exit 0 when nothing
    matches, so a relocated directory leaves this gate covering an empty set.
    Filtered by `is_adr_path`, not "holds a file": `README.md` lives there and
    is the one path the tests assert is *not* an ADR.
    """
    listed = run_git(["ls-tree", "-r", "--name-only", commit, "--", ADR_DIRECTORY])
    return [line for line in listed.splitlines() if is_adr_path(line)]


def changed_lines(diff_range: str, old: str, new: str) -> tuple[set[int], set[int]]:
    """Lines touched, on the old side and on the new side.

    Both are needed, and each side is blind where the other sees. A pure removal
    (`@@ -18 +17,0 @@`) gains no new line, and its content belonged to a section
    of the *old* file. A pure insertion (`@@ -9,0 +10,2 @@`) covers no old line
    — and is worse, because what it inserts can move the boundary it is judged
    against: `## Notes` beneath `## Decision` ends the Decision span at its own
    heading, so the new lines belong to `## Notes` and nothing is reported.

    Both names are passed so rename detection can pair them; with only the new
    one git renders the file as freshly added and reports every line as touched.
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
        if old_count:
            before.update(range(old_start, old_start + old_count))
        else:
            # `-<n>,0` means "after old line n", so the insertion belongs to
            # whichever section holds line n. Only that line: taking n+1 as well
            # attributes an insertion at a section boundary to the *next*
            # section too, and reports expanding the Context as a Decision edit.
            before.add(max(old_start, 1))
        after.update(range(new_start, new_start + new_count))
    return before, after
