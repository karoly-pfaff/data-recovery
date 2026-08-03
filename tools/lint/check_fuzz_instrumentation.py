#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail a fuzz build whose library the fuzzer cannot see.

`-fsanitize=fuzzer` on a fuzz target instruments that target's own translation
unit and nothing it links. Every parser in this project lives in the library, so
without `-fsanitize=fuzzer-no-link` on the library too, libFuzzer runs with
coverage feedback from the harness alone: it mutates blind, and the campaign
measures nothing. Nothing about that is visible from outside — the targets build,
run, and exit zero — which is how it survived every fuzz run this project has
made (story-0606).

The instrumentation leaves SanitizerCoverage symbols in the archive's objects.
Zero of them is the failure; the count itself is not a threshold anyone should
tune, so the gate reports it and judges only whether it is zero.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path

# The symbol family SanitizerCoverage emits: counters, guards and PC tables all
# carry it, and an object compiled without the flag carries none of them.
SANCOV_MARKER = "sancov"


def instrumented_symbols(nm_output: str) -> int:
    """How many SanitizerCoverage symbols `nm` reported."""
    return sum(1 for line in nm_output.splitlines() if SANCOV_MARKER in line)


def read_symbols(archive: Path) -> str:
    """`nm` over one archive, or an empty string when it says nothing."""
    result = subprocess.run(
        ["nm", str(archive)], capture_output=True, text=True, check=False
    )
    return result.stdout


def report(archive: Path, count: int) -> None:
    if count == 0:
        print(
            f"FAIL {archive}: no SanitizerCoverage symbols — the fuzz targets"
            " linking this library mutate without coverage feedback from it;"
            " build with -fsanitize=fuzzer-no-link"
        )
    else:
        print(f"ok   {archive}: {count} SanitizerCoverage symbols")


def check(archives: Sequence[Path]) -> int:
    """Zero when every archive is instrumented; non-zero otherwise.

    A missing archive fails rather than passes: a gate that inspected nothing
    must not report what a gate that found nothing reports.
    """
    if not archives:
        print("FAIL: no archives named; a gate with nothing to inspect is not a pass")
        return 1
    failures = 0
    for archive in archives:
        if not archive.is_file():
            print(f"FAIL {archive}: no such archive to inspect")
            failures += 1
            continue
        count = instrumented_symbols(read_symbols(archive))
        report(archive, count)
        failures += 1 if count == 0 else 0
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail when a fuzz build's library carries no coverage instrumentation."
    )
    parser.add_argument("archives", nargs="+", type=Path)
    arguments = parser.parse_args()
    return check(arguments.archives)


if __name__ == "__main__":
    sys.exit(main())
