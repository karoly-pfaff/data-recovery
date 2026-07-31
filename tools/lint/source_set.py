#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The one answer to "which files do the gates cover".

All three gate scripts — the format driver, the file-length guard and the
duplication detector — import this instead of keeping their own copy, so the
set they cover changes in one place for all of them. `tidy` is the exception,
and states the suffixes itself in `cmake/DevTargets.cmake`, because its file
list has to exist at configure time.

The suffixes are the naming contract (AGENTS.md §1): `.cpp` and `.hpp`, nothing
else. A root that does not exist is a configuration bug, not an empty
contribution — a gate that quietly checks less than it claims would pass while
checking nothing.
"""
from __future__ import annotations

import logging
from collections.abc import Iterable
from pathlib import Path

SOURCE_SUFFIXES = {".cpp", ".hpp"}


def source_files(roots: Iterable[Path | str]) -> list[Path]:
    files: list[Path] = []
    for root in roots:
        base = Path(root)
        if not base.exists():
            raise FileNotFoundError(f"gate root does not exist: {base}")
        files.extend(p for p in base.rglob("*") if p.suffix in SOURCE_SUFFIXES)
    return sorted(files)


def gate_files(roots: Iterable[Path | str]) -> list[Path] | None:
    """The same set, resolved on a gate's behalf: the files, or `None` once the
    reason there are none has been reported. Every gate's `main` answers a bad
    root the same way — say which one, exit 2 — so it is answered here.
    """
    try:
        return source_files(roots)
    except FileNotFoundError as error:
        logging.error("%s", error)
        return None
