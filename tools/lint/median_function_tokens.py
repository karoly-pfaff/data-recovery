#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Report the median function size, in tokens, for one language.

This is the measurement the duplication threshold is chosen from
(`docs/testing/quality-gates.md`). It is a script rather than a one-liner in the
documentation because the one-liner that preceded it computed nothing and exited
0 — an inline `#` swallowed the rest of the line — which is the same failure the
gates themselves are written to avoid, in the command that exists to prove a
number is checkable.

    python3 tools/lint/median_function_tokens.py cpp
    python3 tools/lint/median_function_tokens.py python
"""
from __future__ import annotations

import argparse
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import lizard  # noqa: E402

from source_set import CPP_SUFFIXES, PYTHON_SUFFIXES, source_files  # noqa: E402

LANGUAGES = {"cpp": CPP_SUFFIXES, "python": PYTHON_SUFFIXES}
DEFAULT_ROOTS = ("src", "include", "tools")


def median_tokens(roots: list[str], suffixes: set[str]) -> tuple[int, int, float]:
    files = [str(path) for path in source_files(roots, suffixes)]
    tokens = [
        function.token_count
        for analysis in lizard.analyze_files(files, exts=lizard.get_extensions([]))
        for function in analysis.function_list
    ]
    if not tokens:
        raise SystemExit("no functions found; a median over nothing is not a measurement")
    return len(files), len(tokens), statistics.median(tokens)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("language", choices=sorted(LANGUAGES))
    parser.add_argument("roots", nargs="*", default=list(DEFAULT_ROOTS))
    args = parser.parse_args()

    files, functions, median = median_tokens(args.roots, LANGUAGES[args.language])
    print(f"{args.language}: {files} files, {functions} functions, median {median:g} tokens")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
