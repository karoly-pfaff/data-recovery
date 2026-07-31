#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Enforce the Revenant file-length limit.

Warns at --warn lines and fails (exit 1) at --max lines. clang-tidy has no
file-length check, so this guard covers rule §2 of AGENTS.md. License/blank
lines count: a file over the limit is a signal of too many responsibilities.
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from source_set import gate_files


def line_count(path: Path) -> int:
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        return sum(1 for _ in handle)


def main() -> int:
    parser = argparse.ArgumentParser(description="Enforce the file-length limit.")
    parser.add_argument("--warn", type=int, default=200)
    parser.add_argument("--max", type=int, default=250)
    parser.add_argument("roots", nargs="+")
    args = parser.parse_args()

    logging.basicConfig(format="%(message)s", stream=sys.stderr)
    files = gate_files(args.roots)
    if files is None:
        return 2

    failed = False
    for path in files:
        lines = line_count(path)
        if lines > args.max:
            print(f"ERROR {path}: {lines} lines (max {args.max})", file=sys.stderr)
            failed = True
        elif lines > args.warn:
            print(f"warn  {path}: {lines} lines (warn {args.warn})")

    if failed:
        print("File-length guard failed. Split by responsibility.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
