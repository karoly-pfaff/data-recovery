#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The one answer to "which files do the gates cover".

Every gate script that walks the tree — the format driver, the file-length
guard, the duplication detector, the encoding check and the layer gate — imports
this instead of keeping its own copy, so the set they cover changes in one place
for all of them. `tidy` is the exception, and states the suffixes itself in
`cmake/DevTargets.cmake`, because its file list has to exist at configure time.

**The suffixes are a per-gate argument with no default** (story-0703). Two
of the five gates are inherently C++: `check_format` runs clang-format, and
`check_layering` enforces the C++ include DAG. Widening a shared constant would
hand Python to both — clang-format would reject it, and the layer gate would
read `import` statements as includes. So each gate states what it can analyse,
and the sets below are named for the language rather than for "source". There is
deliberately no default: a gate that does not say what it can analyse is the
thing this argument exists to prevent.

What is *not* owned here, and probably should be: the refusal to pass on an
empty match. `gate_files` owns the missing root; each gate still spells the
empty-set rule itself, and there are five copies of it now.

The suffixes are the naming contract (AGENTS.md §1). A root that does not exist
is a configuration bug, not an empty contribution — a gate that quietly checks
less than it claims would pass while checking nothing.
"""
from __future__ import annotations

import logging
from collections.abc import Iterable
from pathlib import Path

CPP_SUFFIXES = {".cpp", ".hpp"}
PYTHON_SUFFIXES = {".py"}

# Every language a walking gate can be handed. The three gates whose question is
# language-independent ask for this — file length, duplication and encoding; the
# two that parse C++ name their own.
ALL_SUFFIXES = CPP_SUFFIXES | PYTHON_SUFFIXES


def source_files(roots: Iterable[Path | str], suffixes: Iterable[str]) -> list[Path]:
    wanted = set(suffixes)
    files: list[Path] = []
    for root in roots:
        base = Path(root)
        if not base.exists():
            raise FileNotFoundError(f"gate root does not exist: {base}")
        files.extend(p for p in base.rglob("*") if p.suffix in wanted)
    return sorted(files)


def gate_files(roots: Iterable[Path | str], suffixes: Iterable[str]) -> list[Path] | None:
    """The same set, resolved on a gate's behalf: the files, or `None` once the
    reason there are none has been reported. Every gate's `main` answers a bad
    root the same way — say which one, exit 2 — so it is answered here.
    """
    try:
        return source_files(roots, suffixes)
    except FileNotFoundError as error:
        logging.error("%s", error)
        return None
