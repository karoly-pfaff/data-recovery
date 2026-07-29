#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the Revenant benchmark suite against an already-built release.

The suite builds nothing and links nothing: it points at a build directory (or
at an unpacked CI artifact), generates its fixtures with `revenant-imagegen`,
and drives the shipped binaries over them. See docs/performance/benchmarks.md
for what each case measures and which metrics are gated.

    python3 tools/perf/run_benchmarks.py --build-dir build/release --json out.json
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import binaries
import cases
import fixtures
import instructions
import measure
import report
import samples

_MIB = 1 << 20


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build/release",
                        help="build directory or unpacked artifact holding the binaries")
    parser.add_argument("--work-dir", default=None,
                        help="where fixtures live (default: a directory under the system temp)")
    parser.add_argument("--filter", default=None, help="only cases whose name contains this")
    parser.add_argument("--repetitions", type=int, default=5, help="timed runs per case")
    parser.add_argument("--json", default=None, help="write the machine-readable results here")
    parser.add_argument("--no-instructions", action="store_true",
                        help="skip the valgrind pass even where valgrind exists")
    return parser.parse_args()


@dataclass(frozen=True)
class Suite:
    """Everything a case needs that is not the case itself."""

    build_dir: Path
    work_dir: Path
    repetitions: int
    count_instructions: bool


def _fresh_destination(work_dir: Path) -> Path:
    """A destination no previous run has checkpointed into.

    Re-running a recovery into the same destination deliberately resumes where
    the last one stopped (ADR-0008), which would leave every repetition after
    the first with nothing left to scan.
    """
    return Path(tempfile.mkdtemp(prefix="dest-", dir=work_dir))


def run_once(argv: list[str], work_dir: Path) -> measure.Measured:
    destination = _fresh_destination(work_dir)
    try:
        return measure.run_measured([*argv, "--destination", str(destination)])
    finally:
        shutil.rmtree(destination, ignore_errors=True)


def warm_up(argv: list[str], work_dir: Path) -> None:
    """One run whose measurement is thrown away.

    The first read of a freshly written fixture measures the page cache filling
    up, and on Windows it measures the virus scanner reading the file too. Both
    are real costs and neither is the thing under test.
    """
    run_once(argv, work_dir)


def count_instructions(name: str, argv: list[str], work_dir: Path) -> int | None:
    destination = _fresh_destination(work_dir)
    try:
        return instructions.count_for(
            [*argv, "--destination", str(destination)], work_dir / f"cachegrind-{name}.out"
        )
    finally:
        shutil.rmtree(destination, ignore_errors=True)


def work_units(case: cases.Case, output: str, source: Path) -> float:
    """What the run says it did, or — where the fixture is the work — its size."""
    if case.work_label is None:
        return source.stat().st_size / _MIB
    return report.counted(output, case.work_label)


def _instructions_for(case: cases.Case, argv: list[str], suite: Suite) -> int | None:
    if not suite.count_instructions:
        return None
    return count_instructions(case.name, argv, suite.work_dir)


def benchmark(case: cases.Case, suite: Suite) -> dict:
    """One case, measured every way the suite measures."""
    generator = binaries.locate(suite.build_dir, "revenant-imagegen")
    source = fixtures.ensure(generator, case.fixture, suite.work_dir)
    argv = [str(binaries.locate(suite.build_dir, case.binary)), *case.flags,
            "--source", str(source)]
    warm_up(argv, suite.work_dir)
    runs = [run_once(argv, suite.work_dir) for _ in range(suite.repetitions)]
    return report.entry_for(
        name=case.name,
        unit=case.unit,
        timings=samples.summarize([run.seconds for run in runs]),
        peak_rss_bytes=max(run.peak_rss_bytes for run in runs),
        work_units=work_units(case, runs[0].output, source),
        instructions=_instructions_for(case, argv, suite),
    )


def _work_dir(requested: str | None) -> Path:
    path = Path(requested) if requested else Path(tempfile.gettempdir()) / "revenant-perf"
    path.mkdir(parents=True, exist_ok=True)
    return path


def _suite_from(args: argparse.Namespace) -> Suite:
    return Suite(
        build_dir=Path(args.build_dir),
        work_dir=_work_dir(args.work_dir),
        repetitions=args.repetitions,
        count_instructions=not args.no_instructions,
    )


def run_suite(args: argparse.Namespace) -> int:
    suite = _suite_from(args)
    build_type = binaries.require_optimized(suite.build_dir)
    print(f"benchmarks: {build_type} build in {suite.build_dir}, fixtures in {suite.work_dir}")
    entries = [benchmark(case, suite) for case in cases.selected(args.filter)]
    for entry in entries:
        print(report.human_line(entry))
    if args.json:
        Path(args.json).write_text(json.dumps({"benchmarks": entries}, indent=2), encoding="utf-8")
    return 0


def main() -> int:
    args = parse_args()
    try:
        return run_suite(args)
    except (binaries.UnusableBuild, measure.CaseFailed, ValueError) as refusal:
        print(f"benchmarks: {refusal}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
