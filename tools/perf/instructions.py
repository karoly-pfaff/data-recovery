#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Counting the instructions a run executes, where that is possible.

`cachegrind` reports a *simulated* count, which is what makes it repeatable to
a fraction of a percent and therefore the one number worth gating tightly. It
counts every instruction executed, libc included, so it is comparable only
against a baseline built with the same toolchain — a CI gate, not a figure two
developers can trade.

Where valgrind is not available the count is *absent* rather than faked. That
means Windows, which valgrind has no port for; it runs on Linux, FreeBSD,
Solaris/illumos and Android. The Windows analogue would be DynamoRIO or Intel
PIN, and a second implementation is not worth it for a gate CI runs on Linux.
"""
from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path
from typing import Sequence

_VALGRIND = "valgrind"

# `--cache-sim=no` leaves only the instruction count, which is the part that is
# both cheap to simulate and stable across machines.
_TOOL_FLAGS = ("--tool=cachegrind", "--cache-sim=no")

_TOTAL = re.compile(r"I\s+refs:\s+([\d,]+)")


def available() -> bool:
    return shutil.which(_VALGRIND) is not None


def _total_in(output: str) -> int | None:
    found = _TOTAL.search(output)
    if found is None:
        return None
    return int(found.group(1).replace(",", ""))


def count_for(argv: Sequence[str], out_file: Path) -> int | None:
    """Instructions executed by `argv`, or None where nothing can count them.

    One run is enough: a simulated count is deterministic, so repeating it
    would spend minutes confirming the same number.
    """
    if not available():
        return None
    result = subprocess.run(
        [_VALGRIND, *_TOOL_FLAGS, f"--cachegrind-out-file={out_file}", *argv],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None
    return _total_in(result.stderr)
