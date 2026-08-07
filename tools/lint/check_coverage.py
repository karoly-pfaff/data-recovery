#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Enforce the core-logic line-coverage floor (docs/testing/quality-gates.md, gate 8).

Consumes `llvm-cov export -summary-only` JSON. Only files whose repo-relative
path starts with one of the --prefix values count as core logic; tests and
tools are excluded by not being listed. Matching zero files is a hard error:
a gate that silently measures nothing is a fake gate.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path, PurePosixPath


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--min", type=float, required=True, help="floor percentage")
    parser.add_argument("--export", required=True, help="llvm-cov export JSON path")
    parser.add_argument("--root", required=True, help="repo root the filenames are under")
    parser.add_argument("--prefix", action="append", required=True,
                        help="repo-relative dir counted as core (repeatable)")
    return parser.parse_args()


def relative_to_root(filename: str, root: str) -> PurePosixPath | None:
    normalized = PurePosixPath(filename.replace("\\", "/"))
    root_path = PurePosixPath(root.replace("\\", "/"))
    try:
        return normalized.relative_to(root_path)
    except ValueError:
        return None


def is_core(relative: PurePosixPath | None, prefixes: list[str]) -> bool:
    return relative is not None and any(
        relative.is_relative_to(prefix) for prefix in prefixes)


def collect_core_lines(export: dict, root: str, prefixes: list[str]) -> tuple[int, int]:
    covered, count = 0, 0
    for entry in export["data"][0]["files"]:
        relative = relative_to_root(entry["filename"], root)
        if not is_core(relative, prefixes):
            continue
        summary = entry["summary"]["lines"]
        covered += summary["covered"]
        count += summary["count"]
        print(f"  {relative}: {summary['covered']}/{summary['count']}")
    return covered, count


def main() -> int:
    args = parse_args()
    export = json.loads(Path(args.export).read_text(encoding="utf-8"))
    covered, count = collect_core_lines(export, args.root, args.prefix)
    # Not `source_set.refuse_empty_gate`, deliberately: this gate walks no tree.
    # It refuses when the coverage export counted no core *line*, which is the
    # same principle over a different input, and folding it into a file-set
    # helper would make that helper about two things (story-0704).
    if count == 0:
        print("coverage gate: no core files matched — refusing to pass an empty gate")
        return 1
    percent = 100.0 * covered / count
    print(f"core line coverage: {percent:.1f}% (floor {args.min:.1f}%)")
    return 0 if percent >= args.min else 1


if __name__ == "__main__":
    sys.exit(main())
