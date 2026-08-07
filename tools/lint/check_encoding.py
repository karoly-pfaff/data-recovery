#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail the build on a source file that is not plain UTF-8.

clang rejects a byte no other compiler in this project minds. An editing pass
left two raw `0x97` bytes — cp1252's em dash — inside a comment during
story-0609; MSVC and GCC compiled the file in silence, and only clang's
`-Winvalid-utf8` failed, which meant finding out from CI. The cost of the
mistake is a red run; the cost of catching it is reading each file once.

A byte-order mark is rejected too. It is valid UTF-8, but nothing in this tree
writes one, and MSVC and clang do not agree on what it means at the top of a
source file — so it is a difference between toolchains for no benefit.

Discovering which files the gates cover is `source_set`'s job, not this
script's. Exit 0 clean, 1 on an offending file, 2 when the gate could not run
over anything — a gate that checks nothing must never look like a clean tree.
"""
from __future__ import annotations

import argparse
import logging
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from source_set import ALL_SUFFIXES, gate_files

BYTE_ORDER_MARK = b"\xef\xbb\xbf"


@dataclass(frozen=True)
class Offence:
    """Where a file stopped being UTF-8, in the terms a person can act on."""

    line: int
    column: int
    byte: int


def first_offence(path: Path) -> Offence | None:
    raw = path.read_bytes()
    if raw.startswith(BYTE_ORDER_MARK):
        return Offence(line=1, column=1, byte=raw[0])
    try:
        raw.decode("utf-8")
    except UnicodeDecodeError as failure:
        head = raw[: failure.start]
        return Offence(
            line=head.count(b"\n") + 1,
            column=failure.start - head.rfind(b"\n"),
            byte=raw[failure.start],
        )
    return None


def report(path: Path, offence: Offence) -> None:
    logging.error(
        "%s:%d:%d: byte 0x%02X is not valid UTF-8",
        path,
        offence.line,
        offence.column,
        offence.byte,
    )


def check(files: Sequence[Path]) -> int:
    offending = 0
    for path in files:
        offence = first_offence(path)
        if offence is not None:
            report(path, offence)
            offending += 1
    if offending:
        logging.error("encoding gate: %d file(s) are not plain UTF-8", offending)
        return 1
    logging.info("encoding gate: %d file(s) are plain UTF-8", len(files))
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    logging.basicConfig(format="%(message)s", level=logging.INFO)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="+", help="directories to check")
    roots = parser.parse_args(argv).roots

    files = gate_files(roots, ALL_SUFFIXES)
    if files is None:
        return 2
    return check(files)


if __name__ == "__main__":
    sys.exit(main())
