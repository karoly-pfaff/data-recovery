#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare two benchmark result files and rule on regressions.

The thresholds split by what is actually deterministic. Two runs of identical
code on two GitHub-hosted runners are two different machines, so wall-clock
rates are gated loosely — enough to catch the accidental quadratic and nothing
finer. Peak memory and instruction count repeat to a fraction of a percent, so
they are gated tightly and are what actually holds the line.

A drop is only called a regression when it exceeds *both* the threshold and the
baseline's own spread. A runner whose repetitions already disagreed by 20%
cannot tell a 15% regression from its own noise, and a gate that cries wolf on
every noisy run is one people learn to ignore.

A benchmark that appeared in the baseline and not in this run is a failure, not
a pass: a gate that silently stops measuring something is a fake gate (the same
rule `check_coverage.py` applies to an empty match).

No baseline file lives in the repository. An absolute measurement captured on
one machine means nothing on another; this takes two files so CI can compare a
run against `main`'s run on the same runner class, and so a developer can
compare two of their own.

    python3 tools/perf/compare_baseline.py --baseline main.json --current pr.json
"""
from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path

# Two runner models can differ by more than this on wall clock, which is why
# this number gates only the order-of-magnitude mistake.
DEFAULT_RATE_THRESHOLD = 0.25
# A simulated instruction count and a peak working set are properties of the
# program, not of the machine; they repeat to a fraction of a percent.
DEFAULT_EXACT_THRESHOLD = 0.05


@dataclass(frozen=True)
class Metric:
    """One number the gate rules on, and which direction is the bad one."""

    key: str
    label: str
    higher_is_better: bool
    tight: bool

    def worsening(self, before: float, after: float) -> float:
        """How far `after` moved the wrong way, as a fraction of `before`.

        Negative when the change was an improvement. A baseline of zero was
        never measured, so nothing can be said to have fallen from it.
        """
        if before <= 0.0:
            return 0.0
        moved = (before - after) if self.higher_is_better else (after - before)
        return moved / before


METRICS = (
    Metric(key="rate", label="slower", higher_is_better=True, tight=False),
    Metric(key="peak_rss_bytes", label="more memory", higher_is_better=False, tight=True),
    Metric(key="instructions", label="more instructions", higher_is_better=False, tight=True),
)


@dataclass(frozen=True)
class Thresholds:
    rate: float
    exact: float

    def for_metric(self, metric: Metric) -> float:
        return self.exact if metric.tight else self.rate


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, help="result JSON to compare against")
    parser.add_argument("--current", required=True, help="result JSON from this run")
    parser.add_argument("--rate-threshold", type=float, default=DEFAULT_RATE_THRESHOLD,
                        help=f"fractional drop in a rate that counts as a regression"
                             f" (default {DEFAULT_RATE_THRESHOLD})")
    parser.add_argument("--exact-threshold", type=float, default=DEFAULT_EXACT_THRESHOLD,
                        help=f"the same, for peak memory and instruction count"
                             f" (default {DEFAULT_EXACT_THRESHOLD})")
    return parser.parse_args()


def benchmarks_in(path: str) -> dict[str, dict]:
    """Every benchmark in `path`, keyed by name."""
    document = json.loads(Path(path).read_text(encoding="utf-8"))
    return {one["name"]: one for one in document["benchmarks"]}


def _regression(name: str, metric: Metric, pair: tuple[dict, dict], threshold: float) -> str | None:
    before, after = pair
    worsening = metric.worsening(before[metric.key], after[metric.key])
    if worsening <= threshold:
        return None
    # The spread is how far the *timings* disagreed, so it excuses a slower
    # rate and says nothing about a bigger working set or a longer instruction
    # trace — those repeat, which is why they are gated tightly at all.
    if not metric.tight and worsening <= before.get("spread", 0.0):
        return None
    return (f"{name}: {worsening:.1%} {metric.label}"
            f" ({before[metric.key]:,.1f} -> {after[metric.key]:,.1f}),"
            f" beyond the {threshold:.0%} threshold")


def verdicts_for(name: str, before: dict, after: dict, thresholds: Thresholds) -> list[str]:
    """Every complaint about one benchmark, across every metric it reports."""
    complaints = []
    for metric in METRICS:
        if metric.key not in before or metric.key not in after:
            print(f"{name}: {metric.key} not compared (absent from one of the runs)")
            continue
        found = _regression(name, metric, (before, after), thresholds.for_metric(metric))
        if found is not None:
            complaints.append(found)
    return complaints


def failures(baseline: dict[str, dict], current: dict[str, dict], thresholds: Thresholds):
    complaints: list[str] = []
    for name, before in baseline.items():
        after = current.get(name)
        if after is None:
            complaints.append(f"{name}: present in the baseline and missing from this run")
            continue
        complaints.extend(verdicts_for(name, before, after, thresholds))
    return complaints


def report_new(baseline: dict[str, dict], current: dict[str, dict]) -> None:
    for name in current:
        if name not in baseline:
            print(f"new benchmark (not gated): {name}")


def main() -> int:
    args = parse_args()
    baseline = benchmarks_in(args.baseline)
    current = benchmarks_in(args.current)
    thresholds = Thresholds(rate=args.rate_threshold, exact=args.exact_threshold)
    report_new(baseline, current)
    complaints = failures(baseline, current, thresholds)
    for complaint in complaints:
        print(f"performance regression: {complaint}")
    if complaints:
        return 1
    print(f"performance: {len(baseline)} benchmarks within"
          f" {thresholds.rate:.0%} on rates and {thresholds.exact:.0%} on"
          f" peak memory and instruction count")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
