#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare two `revenant-bench --json` result files and rule on regressions.

Every metric the suite reports is a *rate* — MiB/s, entries/s, candidates/s — so
higher is always better and one comparison covers them all. A benchmark that
reported a rate in the baseline and does not appear in the current run is a
failure, not a pass: a gate that silently stops measuring something is a fake
gate (the same rule `check_coverage.py` applies to an empty match).

A drop is only called a regression when it exceeds *both* the threshold and the
baseline's own spread. A runner whose repetitions already disagreed by 20% cannot
tell a 15% regression from its own noise, and a gate that cries wolf on every
noisy run is one people learn to ignore.

No baseline file lives in the repository. An absolute timing captured on one
machine means nothing on another; this takes two files so CI can compare a run
against `main`'s run on the same runner class, and so a developer can compare two
of their own.

    python3 tools/perf/compare_baseline.py --baseline main.json --current pr.json
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, help="result JSON to compare against")
    parser.add_argument("--current", required=True, help="result JSON from this run")
    parser.add_argument("--threshold", type=float, default=0.10,
                        help="fractional drop that counts as a regression (default 0.10)")
    return parser.parse_args()


def rates_in(path: str) -> dict[str, dict]:
    """Every benchmark in `path`, keyed by name."""
    document = json.loads(Path(path).read_text(encoding="utf-8"))
    return {one["name"]: one for one in document["benchmarks"]}


def drop_fraction(baseline_rate: float, current_rate: float) -> float:
    """How far `current_rate` fell below `baseline_rate`, as a fraction of it.

    Negative when the current run was faster. A baseline of zero was never
    measured, so nothing can be said to have fallen from it.
    """
    if baseline_rate <= 0.0:
        return 0.0
    return (baseline_rate - current_rate) / baseline_rate


def verdict_for(name: str, before: dict, after: dict | None, threshold: float) -> str | None:
    """The complaint about one benchmark, or None when there is none."""
    if after is None:
        return f"{name}: present in the baseline and missing from this run"
    drop = drop_fraction(before["rate"], after["rate"])
    if drop <= threshold:
        return None
    if drop <= before.get("spread", 0.0):
        return None
    return (f"{name}: {drop:.1%} slower ({before['rate']:.1f} -> {after['rate']:.1f}"
            f" {before['unit']}), beyond the {threshold:.0%} threshold and the"
            f" baseline's own {before.get('spread', 0.0):.1%} spread")


def failures(baseline: dict[str, dict], current: dict[str, dict], threshold: float) -> list[str]:
    complaints = (verdict_for(name, before, current.get(name), threshold)
                  for name, before in baseline.items())
    return [complaint for complaint in complaints if complaint is not None]


def report_new(baseline: dict[str, dict], current: dict[str, dict]) -> None:
    for name in current:
        if name not in baseline:
            print(f"new benchmark (not gated): {name}")


def main() -> int:
    args = parse_args()
    baseline = rates_in(args.baseline)
    current = rates_in(args.current)
    report_new(baseline, current)
    complaints = failures(baseline, current, args.threshold)
    for complaint in complaints:
        print(f"performance regression: {complaint}")
    if complaints:
        return 1
    print(f"performance: {len(baseline)} benchmarks within {args.threshold:.0%} of baseline")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
