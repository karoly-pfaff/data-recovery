#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail the build when a block of C++ lives in more than one place.

The gate `docs/testing/quality-gates.md` calls gate 4. `lizard` does the part
that needs a C++ parser: it hashes *unified* tokens, so two blocks that differ
only in what their identifiers are called are still one block. Everything the
merge depends on is here — which blocks count, what is reported, and the exit
code — because `lizard`'s own command line has no threshold flag, always exits
zero, and prints prose.

**The threshold is per copy.** `lizard`'s `min_duplicate_tokens` counts the
tokens of every copy added together, so a family of twelve short blocks clears
a bar no single copy comes near. The threshold passed to `lizard` is therefore
a floor (for two copies the two measures coincide) and this module keeps only
the blocks whose copies individually reach it.

**Only code counts.** A block is reported only when *every one* of its sites
lies inside a function body. `lizard` unifies identifiers *and* keywords and
collapses literals, so any two runs of layout constants hash alike — and every
byte parser here opens with an include list, a namespace and a run of on-disk
offsets. Those are different facts wearing the only shape C++ has for stating
them; no refactoring makes them one, so a gate that reported them would be red
for good. Duplicated *declarations* are review's job (`docs/code-quality.md`),
not this gate's.

The one number that is not filtered that way is the duplicate rate, which is
`lizard`'s own and covers every family it groups at the threshold. It is a
trend line printed beside the verdict, never the verdict.
"""
from __future__ import annotations

import argparse
import logging
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

import lizard
from lizard_ext.lizardduplicate import LizardExtension, NestingStackWithUnifiedTokens

from source_set import source_files


@dataclass(frozen=True)
class Site:
    """One place a duplicated block lives."""

    path: str
    start_line: int
    end_line: int


@dataclass(frozen=True)
class Block:
    """One duplicated block: its size in tokens, and every site holding it."""

    tokens: int
    sites: tuple[Site, ...]


@dataclass(frozen=True)
class Report:
    blocks: list[Block]
    rate: float


class _SizedDuplicates(LizardExtension):
    """`lizard`'s duplicate finder, with each block's own size kept.

    The extension reports a block as a list of line ranges and throws the token
    length away, and the token length is what the threshold is in. It survives
    the one seam that has it: the node indices a block was built from.
    """

    def _create_code_snippets(self, start_and_ends):
        snippets = super()._create_code_snippets(start_and_ends)
        start, end = start_and_ends[0]
        return Block(
            tokens=end - start + NestingStackWithUnifiedTokens.SAMPLE_SIZE,
            sites=tuple(
                Site(snippet.file_name, snippet.start_line, snippet.end_line)
                for snippet in snippets
            ),
        )


def _holds_code(site: Site, bodies: dict[str, list[tuple[int, int]]]) -> bool:
    return any(
        start <= site.end_line and site.start_line <= end
        for start, end in bodies.get(site.path, ())
    )


def duplicate_blocks(files: Sequence[Path | str], *, min_tokens: int) -> Report:
    extension = _SizedDuplicates()
    analysis = lizard.analyze_files(
        [str(path) for path in files], exts=lizard.get_extensions([]) + [extension]
    )
    # Draining the analysis is what feeds the extension; the function ranges it
    # yields on the way past are what tells code from declarations afterwards.
    bodies = {
        info.filename: [(fn.start_line, fn.end_line) for fn in info.function_list]
        for info in analysis
    }
    blocks = [
        block
        for block in extension.get_duplicates(min_duplicate_tokens=min_tokens)
        if block.tokens >= min_tokens
        and all(_holds_code(site, bodies) for site in block.sites)
    ]
    blocks.sort(key=lambda block: -block.tokens)
    # `duplicate_rate()` is only set once the generator above is drained, and it
    # is `None` until then. Not defended against: a `None` here would mean the
    # scan never ran, and printing that as 0.00% is how a gate reports a clean
    # tree it never looked at.
    return Report(blocks=blocks, rate=float(extension.duplicate_rate()))


def run_gate(files: Sequence[Path | str], *, min_tokens: int) -> int:
    if not files:
        logging.error("no source files matched; refusing to pass an empty gate")
        return 2

    report = duplicate_blocks(files, min_tokens=min_tokens)
    for block in report.blocks:
        logging.error("duplicated block, %d tokens per copy:", block.tokens)
        for site in block.sites:
            logging.error("    %s:%d-%d", site.path, site.start_line, site.end_line)
    print(
        f"duplication gate: {len(report.blocks)} block(s) at or above "
        f"{min_tokens} tokens per copy; duplicate rate {report.rate * 100:.2f}%"
    )
    return 1 if report.blocks else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail on blocks duplicated at or above a token threshold."
    )
    parser.add_argument(
        "--min-tokens",
        type=int,
        required=True,
        help="smallest duplicated block, in tokens per copy, that fails the gate",
    )
    parser.add_argument("roots", nargs="+")
    args = parser.parse_args()

    logging.basicConfig(format="%(message)s", stream=sys.stderr)
    try:
        files = source_files(args.roots)
    except FileNotFoundError as error:
        logging.error("%s", error)
        return 2
    return run_gate(files, min_tokens=args.min_tokens)


if __name__ == "__main__":
    raise SystemExit(main())
