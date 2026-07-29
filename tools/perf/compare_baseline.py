#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare two benchmark result files and rule on regressions.

The thresholds split by what survives being measured on a different machine, and
each one is a number the suite measured rather than a number somebody liked. Two
runs of identical code on two GitHub-hosted runners are two different machines,
so wall-clock rates are gated loosely — enough to catch the accidental quadratic
and nothing finer. An instruction count is simulated and repeats to a
hundredth of a percent, so it is what actually holds the line.

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


@dataclass(frozen=True)
class Metric:
    """One number the gate rules on, and which direction is the bad one.

    Each carries its own threshold, and there is no flag to override it. A
    threshold that can be passed on the command line is a threshold somebody
    will pass on the command line to turn a red run green.
    """

    key: str
    label: str
    higher_is_better: bool
    threshold: float
    # Whether the baseline's timing spread can excuse a move. It describes how
    # far the *timings* disagreed, so it speaks for a rate and for nothing else.
    spread_excuses: bool

    def worsening(self, before: float, after: float) -> float:
        """How far `after` moved the wrong way, as a fraction of `before`.

        Negative when the change was an improvement. A baseline of zero was
        never measured, so nothing can be said to have fallen from it.
        """
        if before <= 0.0:
            return 0.0
        moved = (before - after) if self.higher_is_better else (after - before)
        return moved / before


# Every threshold below is a measurement, not a preference. Two consecutive CI
# runs of near-identical code, on two runner machines, disagreed by: 0.06% on
# instruction count (the worst of four cases), 0.06% on peak memory for three
# cases and 5.3% for the fourth, and up to 22.5% on a rate.
#
# So the instruction count is gated at 5%, eighty times its observed noise. Peak
# memory is gated at 10%: the case that moved 5.3% is the only one whose
# footprint is small enough for a single allocator arena to be 5% of it, and a
# real leak or a mis-sized buffer moves memory by far more than 10% — the carve
# bound alone is 64 MiB. A rate is gated at 25%, which catches the accidental
# quadratic and deliberately nothing finer.
METRICS = (
    Metric(key="rate", label="slower", higher_is_better=True,
           threshold=0.25, spread_excuses=True),
    Metric(key="peak_rss_bytes", label="more memory", higher_is_better=False,
           threshold=0.10, spread_excuses=False),
    Metric(key="instructions", label="more instructions", higher_is_better=False,
           threshold=0.05, spread_excuses=False),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, help="result JSON to compare against")
    parser.add_argument("--current", required=True, help="result JSON from this run")
    return parser.parse_args()


def benchmarks_in(path: str) -> dict[str, dict]:
    """Every benchmark in `path`, keyed by name."""
    document = json.loads(Path(path).read_text(encoding="utf-8"))
    return {one["name"]: one for one in document["benchmarks"]}


def _regression(name: str, metric: Metric, before: dict, after: dict) -> str | None:
    worsening = metric.worsening(before[metric.key], after[metric.key])
    if worsening <= metric.threshold:
        return None
    if metric.spread_excuses and worsening <= before.get("spread", 0.0):
        return None
    return (f"{name}: {worsening:.1%} {metric.label}"
            f" ({before[metric.key]:,.1f} -> {after[metric.key]:,.1f}),"
            f" beyond the {metric.threshold:.0%} threshold")


def verdicts_for(name: str, before: dict, after: dict) -> list[str]:
    """Every complaint about one benchmark, across every metric it reports."""
    complaints = []
    for metric in METRICS:
        if metric.key not in before or metric.key not in after:
            print(f"{name}: {metric.key} not compared (absent from one of the runs)")
            continue
        found = _regression(name, metric, before, after)
        if found is not None:
            complaints.append(found)
    return complaints


def failures(baseline: dict[str, dict], current: dict[str, dict]) -> list[str]:
    complaints: list[str] = []
    for name, before in baseline.items():
        after = current.get(name)
        if after is None:
            complaints.append(f"{name}: present in the baseline and missing from this run")
            continue
        complaints.extend(verdicts_for(name, before, after))
    return complaints


def report_new(baseline: dict[str, dict], current: dict[str, dict]) -> None:
    for name in current:
        if name not in baseline:
            print(f"new benchmark (not gated): {name}")


def main() -> int:
    args = parse_args()
    baseline = benchmarks_in(args.baseline)
    current = benchmarks_in(args.current)
    report_new(baseline, current)
    complaints = failures(baseline, current)
    for complaint in complaints:
        print(f"performance regression: {complaint}")
    if complaints:
        return 1
    stated = ", ".join(f"{metric.key} {metric.threshold:.0%}" for metric in METRICS)
    print(f"performance: {len(baseline)} benchmarks within their thresholds ({stated})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
