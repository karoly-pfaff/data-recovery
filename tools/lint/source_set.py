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

**The refusal to pass on an empty match is owned here too** (story-0704). It was
spelled out in four of the gates that walk the tree; the rule is stated once
here now. `gate_files` calls
it at the discovery boundary, so a gate that resolves no files stops without
having to remember to — including `check_file_length`, which enforces
AGENTS.md §2's headline number and never had the guard at all. Two gates call it
a second time inside their own `run_gate`, because those are public functions the
tests hand a list directly, and a list that arrives empty by some other route
must not report a clean pass either. Same knowledge, one statement, two call
sites.

The suffixes are the naming contract (AGENTS.md §1). A root that does not exist
is a configuration bug, not an empty contribution — a gate that quietly checks
less than it claims would pass while checking nothing.
"""
from __future__ import annotations

import logging
from collections.abc import Iterable, Sequence
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


def report_empty_gate(roots: Sequence[Path | str] = (), gate: str = "") -> None:
    """Say that this gate has nothing to inspect, naming what it can.

    A gate handed no files must fail. It cannot pass: a checker that inspected
    nothing reports the same green as one that looked and found nothing, and the
    two are indistinguishable to whoever reads the log.

    The message names the gate and the roots when the caller knows them. Without
    both, a mistyped root produces a refusal saying neither which gate stopped
    nor what it was pointed at — the per-gate copies this replaced said the
    second, and `check_encoding` said the first.
    """
    who = f"{gate}: " if gate else ""
    if roots:
        logging.error("%sno source files under %s; refusing to pass an empty gate", who,
                      " ".join(str(root) for root in roots))
    else:
        logging.error("%sno source files matched; refusing to pass an empty gate", who)


def refuse_empty_gate(
    files: Sequence[Path | str], roots: Sequence[Path | str] = (), gate: str = ""
) -> bool:
    """Whether this gate has nothing to inspect — reported, not merely returned.

    The predicate half for callers that hold a file set and no roots: the two
    `run_gate` functions, which are public and may be handed a list by any route.
    """
    if files:
        return False
    report_empty_gate(roots, gate)
    return True


def _files_per_root(
    roots: Sequence[Path | str], suffixes: Iterable[str]
) -> list[tuple[Path, list[Path]]] | None:
    """Each root paired with what it holds, or `None` once a bad root was named."""
    per_root: list[tuple[Path, list[Path]]] = []
    for root in roots:
        try:
            per_root.append((Path(root), source_files([root], suffixes)))
        except FileNotFoundError as error:
            logging.error("%s", error)
            return None
    return per_root


def gate_files(
    roots: Iterable[Path | str], suffixes: Iterable[str], gate: str = ""
) -> list[Path] | None:
    """The set a gate should inspect, or `None` once the reason there is none has
    been reported.

    **Each root is judged on its own.** A gate handed `src include tools` where
    `include` holds nothing is covering less than it claims, and the union being
    non-empty hides that completely — the failure this refusal exists to stop,
    one level up. So an empty root is refused by name even when its siblings are
    full, and the remedy for a root that deliberately contributes nothing is to
    drop it from that gate's list rather than to add a flag here.

    `roots` is materialised because it is walked twice; a generator would
    otherwise be consumed by the first pass and lose its names to the second.
    """
    resolved = list(roots)
    per_root = _files_per_root(resolved, suffixes)
    if per_root is None:
        return None
    barren = [root for root, found in per_root if not found]
    if barren or not resolved:
        report_empty_gate(barren, gate)
        return None
    return sorted(path for _, found in per_root for path in found)
