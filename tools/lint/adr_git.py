#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Running git, and the flags that decide what it will say.

Separated from `adr_range` because talking to a subprocess and reasoning about a
range are different jobs, and because everything here exists for one reason: the
answer git gives can be emptied by the repository, the reader's configuration or
the working directory, and four such channels each made this gate exit 0 on the
one commit it was written to catch. `docs/testing/quality-gates.md` lists them.

Every failure becomes `CannotAnswer` — including the ones that are not a
non-zero exit. No `git` on the path and a non-UTF-8 byte both used to escape as
a traceback and exit 1, the code reserved for "found a violation".
"""
from __future__ import annotations

import subprocess
from functools import lru_cache

from adr_markdown import CannotAnswer

# Each defeats one way the repository or the reader can empty this gate's
# input; `quality-gates.md` tabulates which. Flags, because a command-line flag
# outranks configuration.
DIFF_FLAGS = ("--no-ext-diff", "--no-textconv", "--text", "-M")


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


def run_git(args: list[str]) -> str:
    """Run git, turning every way it can fail into the one typed fault.

    Including the ways that are not a non-zero exit: no `git` on PATH, and a
    non-UTF-8 byte in an ADR (`docs/` is outside `check_encoding.py`'s roots, so
    nothing else catches that first). Both escaped as a traceback and exit 1 —
    the code `quality-gates.md` reserves for "found a violation".
    """
    try:
        finished = subprocess.run(
            ["git", *args], cwd=top_level(), capture_output=True, check=False
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
