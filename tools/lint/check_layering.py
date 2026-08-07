#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Enforce the direction of the layer DAG.

`docs/architecture/overview.md` says "each layer depends only on the layer
below", and until this gate existed nothing checked it: one static library holds
every layer and the whole of `src/` is an include path for all of it, so the
compiler cannot object. An upward include entered the tree in the fourth pull
request this project ever merged and survived twelve more, passing every gate on
each of them.

What is checked is *direction*, not adjacency. A file in layer L may include any
layer at or below L. Enforcing adjacency literally would fail hundreds of edges
whose only sin is that `fs/` reads `Result<T>` without asking `volume/` first,
and a gate that has to be argued with on day one is a gate that gets switched
off. The DAG's real claim is that the arrows point one way.
"""
from __future__ import annotations

import argparse
import logging
import re
import sys
from pathlib import Path

from source_set import CPP_SUFFIXES, gate_files

# Top to bottom, taken from the diagram in docs/architecture/overview.md. This
# is the gate's whole specification and the only machine-readable copy of it:
# changing the diagram without changing this list is a review finding, not
# something the gate can catch.
#
# Two departures from the diagram, both measured before being taken.
# `core/io` is folded into `core`: the diagram gives Device I/O its own rung but
# both live under one directory, and no file in `core/` outside `io/` includes
# `core/io/` today, so splitting the node would buy a sub-directory rule the
# tree has never needed. `tools` is added above `cli`: `tools/imagegen` consumes
# the library exactly as the frontends do, and giving it the top node buys one
# rule worth having — nothing in `src/` or `include/` may include the fixture
# builders.
LAYER_ORDER = ("tools", "cli", "recovery", "carve", "fs", "volume", "core")

# `imagegen/` is where the `tools` node's headers actually live.
INCLUDE_ALIASES = {"imagegen": "tools"}

# Every line whose first non-blank token is `#include` of a quoted path. Angle
# brackets are the standard library and third-party headers, which have no layer.
# Any `#if` around the line is ignored on purpose: the conservatism runs in the
# safe direction, over-reporting a conditional include rather than under.
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


class Unclassifiable(Exception):
    """Something under a walked root the layer list cannot account for.

    Always exit 2, never a pass: a gate that skips what it does not understand
    checks less than it claims while reporting success, which is the whole
    failure this gate exists against.
    """


def file_layer(path: Path, roots: list[Path]) -> str:
    """Which layer a file belongs to, from its path *relative to its root*.

    Relative to the root, and not by searching the whole path: an absolute path
    contains whatever the checkout happens to sit under, and this project's own
    name is one of the words that would otherwise be matched — `git clone …
    revenant` alone would have broken the gate, and a checkout under any
    directory called `tools` would have classified every file as the top layer
    and reported the tree clean.
    """
    for root in roots:
        if not path.is_relative_to(root):
            continue
        rest = path.relative_to(root).parts
        if root.name == "tools":
            return "tools"
        # `include/revenant/<layer>/…` against `src/<layer>/…`: one public root
        # with a namespace directory, one private root without.
        wanted = rest[1:] if root.name == "include" and rest[:1] == ("revenant",) else rest
        if len(wanted) >= 2 and wanted[0] in LAYER_ORDER:
            return wanted[0]
        raise Unclassifiable(f"{path}: '{'/'.join(wanted[:1]) or path.name}' is not a declared layer")
    raise Unclassifiable(f"{path}: not under any walked root")


def included_layer(spelling: str) -> str | None:
    """Which layer an include names, or `None` when it names no layer.

    Both spellings of a public header resolve the same way: `revenant/fs/X.hpp`
    and `fs/X.hpp` are one edge. An include with no directory part, or one whose
    first part is not a layer, is intra-layer or third-party — `formats/` inside
    `carve/`, `nlohmann/json.hpp` — and is not an edge between layers.

    A spelling with a `.` or `..` component stops the gate instead. Those
    resolve against the including file's own directory rather than an include
    root, so `"../fs/X.hpp"` from `volume/` is a real upward edge that this
    function cannot see — and treating what it cannot classify as "not an edge"
    would make the gate silently skippable by one character.
    """
    parts = spelling.split("/")
    if "." in parts or ".." in parts:
        raise Unclassifiable(
            f'#include "{spelling}": a relative spelling cannot be resolved to a layer'
        )
    if parts[0] == "revenant":
        parts = parts[1:]
    if len(parts) < 2:
        return None
    head = INCLUDE_ALIASES.get(parts[0], parts[0])
    return head if head in LAYER_ORDER else None


def violations_in(path: Path, layer: str) -> tuple[list[str], int]:
    """Upward includes in one file, and how many cross-layer edges it has."""
    found: list[str] = []
    crossings = 0
    here = LAYER_ORDER.index(layer)
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for number, line in enumerate(handle, start=1):
            match = INCLUDE_PATTERN.match(line)
            if match is None:
                continue
            target = included_layer(match.group(1))
            if target is None or target == layer:
                continue
            crossings += 1
            if LAYER_ORDER.index(target) < here:
                # ASCII only: this line is emitted on the failure path, where
                # stderr is a pipe and Python falls back to the ANSI code page.
                # A non-Latin one would turn a named violation into a traceback.
                found.append(
                    f"{path}:{number}: {layer}/ must not include {target}/ "
                    f'- #include "{match.group(1)}"'
                )
    return found, crossings


def main() -> int:
    parser = argparse.ArgumentParser(description="Enforce the layer DAG's direction.")
    parser.add_argument("roots", nargs="+")
    args = parser.parse_args()

    logging.basicConfig(format="%(message)s", stream=sys.stderr)
    files = gate_files(args.roots, CPP_SUFFIXES)
    if files is None:
        return 2

    roots = [Path(root) for root in args.roots]
    failures: list[str] = []
    crossings = 0
    for path in files:
        try:
            found, counted = violations_in(path, file_layer(path, roots))
        except Unclassifiable as error:
            logging.error("%s", error)
            logging.error(
                "Declare it in LAYER_ORDER, or the gate checks less than it claims."
            )
            return 2
        failures.extend(found)
        crossings += counted

    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        print(
            f"layer gate: {len(failures)} upward include(s) "
            f"among {crossings} cross-layer edges",
            file=sys.stderr,
        )
        return 1
    print(f"layer gate: clean; {crossings} cross-layer edges, none upward")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
