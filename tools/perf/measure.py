#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One measured run of a shipped binary.

The measurement lives outside the process: the harness starts a binary and
watches it. That is what makes peak memory free, instruction counting possible,
and real I/O visible — the fixture is a file, not an in-memory device — and it
is also what a user experiences, which is the number that matters for a tool
pointed at a failing disk.

A case whose subprocess exits non-zero is a failure, never a very fast run.
"""
from __future__ import annotations

import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

import peakmemory


class CaseFailed(RuntimeError):
    """A benchmark case did not run, so it has no measurement."""


@dataclass(frozen=True)
class Measured:
    """What one run of a case cost, and what it said it did."""

    seconds: float
    peak_rss_bytes: int
    output: str


def _start(argv: Sequence[str], cwd: Path | None) -> subprocess.Popen:
    try:
        return subprocess.Popen(
            list(argv),
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
    except OSError as failure:
        raise CaseFailed(f"could not run {argv[0]}: {failure}") from failure


def run_measured(argv: Sequence[str], cwd: Path | None = None) -> Measured:
    """Runs `argv` to completion, timed and watched. Raises on a failed run."""
    started = time.perf_counter()
    process = _start(argv, cwd)
    watch = peakmemory.Watch(process)
    with process.stdout as pipe:
        output = pipe.read()
    exit_code, peak_rss_bytes = watch.reap()
    elapsed = time.perf_counter() - started
    if exit_code != 0:
        raise CaseFailed(f"{argv[0]} exited {exit_code}:\n{output}")
    return Measured(seconds=elapsed, peak_rss_bytes=peak_rss_bytes, output=output)
