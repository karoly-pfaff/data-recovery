#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Drive clang-format over the tree in bounded batches.

The format targets used to hand clang-format every source file as one command
line, and the tree outgrew Windows' 32,767-character `CreateProcess` limit —
both targets died of a launcher error before clang-format ran (story-0607).
This driver discovers the file set at run time and spends it in batches that
never exceed a stated character budget, so growth adds invocations, not
length. A formatting violation in any batch fails the gate naming the file; a
clean tree passes; a file set that matches nothing refuses to pass.
"""
from __future__ import annotations

import argparse
import logging
import subprocess
import sys
from collections.abc import Callable, Iterable, Sequence
from pathlib import Path

SOURCE_SUFFIXES = {".cpp", ".hpp", ".cc", ".h"}

# CreateProcess refuses 32,767 characters; the budget covers only the file
# arguments, so it leaves generous room for the program path and the flags.
DEFAULT_BUDGET = 24_000

Runner = Callable[[Sequence[str]], tuple[int, str]]


def source_files(roots: Iterable[Path | str]) -> list[Path]:
    files: list[Path] = []
    for root in roots:
        base = Path(root)
        if not base.exists():
            continue
        files.extend(p for p in base.rglob("*") if p.suffix in SOURCE_SUFFIXES)
    return sorted(files)


def batch_by_length(files: Sequence[Path], budget: int) -> list[list[Path]]:
    batches: list[list[Path]] = []
    current: list[Path] = []
    used = 0
    for path in files:
        cost = len(str(path)) + 1
        if current and used + cost > budget:
            batches.append(current)
            current, used = [], 0
        current.append(path)
        used += cost
    if current:
        batches.append(current)
    return batches


def subprocess_runner(argv: Sequence[str]) -> tuple[int, str]:
    completed = subprocess.run(argv, capture_output=True, text=True, check=False)
    return completed.returncode, (completed.stdout + completed.stderr).strip()


def run_gate(
    files: Sequence[Path],
    *,
    fix: bool,
    budget: int,
    runner: Runner,
    clang_format: str = "clang-format",
) -> int:
    if not files:
        logging.error("no source files matched; refusing to pass an empty gate")
        return 2

    mode = ["-i"] if fix else ["--dry-run", "--Werror"]
    failed = False
    for batch in batch_by_length(files, budget):
        argv = [clang_format, "--style=file", *mode, *map(str, batch)]
        code, output = runner(argv)
        if code != 0:
            failed = True
            logging.error("%s", output or f"clang-format exited {code}")
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check or fix clang-format compliance in bounded batches."
    )
    parser.add_argument("--fix", action="store_true", help="reformat in place")
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--max-command-chars", type=int, default=DEFAULT_BUDGET)
    parser.add_argument("roots", nargs="+")
    args = parser.parse_args()

    logging.basicConfig(format="%(message)s", stream=sys.stderr)
    outcome = run_gate(
        source_files(args.roots),
        fix=args.fix,
        budget=args.max_command_chars,
        runner=subprocess_runner,
        clang_format=args.clang_format,
    )
    if outcome == 0 and not args.fix:
        print("format gate: clean")
    return outcome


if __name__ == "__main__":
    raise SystemExit(main())
