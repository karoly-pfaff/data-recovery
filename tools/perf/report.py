#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a measured case becomes: one JSON object, and one line a person reads.

The work units are read out of the run's own summary rather than assumed. A
rate computed from a guess about what the case processed would be a guess about
the rate, and `RunSummary` already prints `regions scanned`, `carve candidates`
and `filesystem entries` in a greppable form.
"""
from __future__ import annotations

import re

import samples


def counted(output: str, label: str) -> float:
    """The number a run printed after `label`, e.g. `carve candidates 8192`.

    A label the run never printed is refused: the alternative is inventing the
    denominator of a rate.
    """
    found = re.search(rf"{re.escape(label)} (\d+)", output)
    if found is None:
        raise ValueError(f"the run never reported '{label}'; it said:\n{output}")
    return float(found.group(1))


def _rate(work_units: float, median_seconds: float) -> float:
    """Work per second. A run the clock could not see has no rate to report."""
    if median_seconds <= 0.0:
        return 0.0
    return work_units / median_seconds


def entry_for(
    *,
    name: str,
    unit: str,
    timings: samples.Samples,
    peak_rss_bytes: int | None,
    work_units: float,
    instructions: int | None,
) -> dict:
    """One benchmark's result, in the shape `compare_baseline.py` reads.

    A measurement that was not taken is *absent* rather than zero, and the two
    that can be absent are absent for reasons: a machine with no valgrind
    counted no instructions, and a case whose metric is the ratio of two runs
    has no single peak working set to report. A zero would read as a program
    that executed nothing and allocated nothing.
    """
    entry = {
        "name": name,
        "unit": unit,
        "median_seconds": timings.median,
        "min_seconds": timings.minimum,
        "max_seconds": timings.maximum,
        "spread": timings.spread,
        "work_units": work_units,
        "rate": _rate(work_units, timings.median),
    }
    if peak_rss_bytes is not None:
        entry["peak_rss_bytes"] = peak_rss_bytes
    if instructions is not None:
        entry["instructions"] = instructions
    return entry


def _instruction_note(entry: dict) -> str:
    if "instructions" not in entry:
        return ""
    return f", {entry['instructions'] / 1e6:.1f}M instructions"


def _memory_note(entry: dict) -> str:
    if "peak_rss_bytes" not in entry:
        return ""
    return f", peak RSS {entry['peak_rss_bytes'] / (1 << 20):.1f} MiB"


def human_line(entry: dict) -> str:
    """The case, what it managed, and how much its repetitions disagreed."""
    return (
        f"{entry['name']}: {entry['rate']:,.2f} {entry['unit']}"
        f" (median {entry['median_seconds']:.3f}s, spread {entry['spread']:.1%}"
        f"{_memory_note(entry)}{_instruction_note(entry)})"
    )
