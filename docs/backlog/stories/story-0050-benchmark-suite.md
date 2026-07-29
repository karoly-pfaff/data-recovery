<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0050: The benchmark suite, and the gate that reads it

- Epic: [epic-m5-hardening-release](../epic-m5-hardening-release.md)
- Status: Done
- Size: M

## Goal

Make "faster" a thing that can be shown rather than claimed. The performance
strategy forbids any optimization that is not justified by a benchmark, so the
benchmark has to exist before the first optimization does — this is that
benchmark, plus the comparison that turns a pair of runs into a pass or a fail.

## Design references

- [performance/strategy.md](../../performance/strategy.md) — "Measure first. No
  optimization lands without a benchmark showing a real win."
- [performance/benchmarks.md](../../performance/benchmarks.md) — the suite's
  specification: what is measured, how, and the 10% regression policy. This
  story implements it.
- [`tools/lint/check_coverage.py`](../../../tools/lint/check_coverage.py) — the
  precedent this follows for a gate script: a pure function over JSON, driven in
  `ctest` by checked-in fixtures, so the *gate itself* is tested.

## Scope

1. **The harness** (`tools/perf/`) — timing, statistics, and reporting, built as
   `revenant-bench`.
2. **Four benchmarks** — `scan-throughput`, `carve-validate`, `ntfs-enumerate`,
   `end-to-end-hybrid`. The fifth the spec lists, `scan-simd-vs-portable`, needs
   a SIMD path to compare against and belongs to
   [story-0051](story-0051-simd-scan.md).
3. **`tools/perf/compare_baseline.py`** — the regression gate: two result files
   in, a verdict out, at a stated threshold.
4. **Tests** — for the statistics, for the JSON, and for the gate's own verdict
   logic against checked-in fixtures.

## Design decisions

**The statistics are code, not a library's black box.** The whole suite exists
to answer "is this faster", and that answer is a median and a spread. Writing
them as pure functions costs about thirty lines and makes them *testable* — a
benchmark harness whose own arithmetic is unverified is a strange thing to gate
merges on. It also keeps a third-party dependency out of a manifest that has one
entry, which matters more than usual right now: CI cannot currently verify that a
new dependency resolves on the Linux runner.

**The median is the headline and the spread is the veto.** A single fast run
proves nothing on a machine with other work on it. The report carries min, max
and median, and the comparison refuses to call a change a regression when the
baseline's own spread already covers it — otherwise the gate cries wolf on every
noisy runner and gets ignored, which is worse than not having it.

**No baseline file is checked in.** An absolute number captured on one machine is
meaningless on another, and a repository is the wrong place to keep a machine's
measurement. `compare_baseline.py` takes *two* result files, which is what lets
CI compare a PR's run against `main`'s run on the same runner class, and what
lets a developer compare two of their own runs. The spec's "captured in CI and
stored" is an artifact, not a commit.

**Benchmarks build with the tests and refuse to be believed without optimization.**
Building them everywhere keeps them compiling, formatted and tidied like the rest
of the tree; running them under a Debug or sanitized build produces numbers that
mean nothing. So `revenant-bench` prints a loud warning when it was not built
optimized, rather than quietly reporting a figure someone might paste into a pull
request.

**`end-to-end-hybrid` measures discovery, not extraction.** The spec calls for a
full hybrid run, and this runs the whole engine — the filesystem walk over every
partition and the carve scan over what it did not claim. What it leaves out is
writing the recovered files, because that measures the *destination's* disk and
swamps the thing under test with variance that has nothing to do with the code.
`benchmarks.md` now says so.

**Work units travel with the benchmark, so the rate is the benchmark's own.**
Each case returns how much work it did — bytes scanned, entries enumerated,
candidates validated — and the harness divides. A rate computed anywhere else
would have to guess at what the case actually processed.

## Acceptance criteria

- [x] `statisticsOf` returns the median, min and max of a set of timings, and a
      spread relative to the median; a single sample has zero spread.
- [x] An empty sample set is a zero statistic rather than undefined behaviour.
- [x] `revenant-bench` runs every benchmark, or only those matching `--filter`.
- [x] `--json <path>` writes one object per benchmark: name, unit, median, min,
      max, spread, work units and rate.
- [x] The human summary names each benchmark, its rate and its spread.
- [x] A build without `NDEBUG` prints a warning that the numbers are not
      comparable.
- [x] `compare_baseline.py` exits 0 when nothing regressed beyond the threshold,
      and non-zero naming the benchmark when something did.
- [x] A regression inside the baseline's own spread is not called a regression.
- [x] A benchmark present in the baseline but missing from the current run is a
      failure, not a silent pass.
- [x] A benchmark that is new in the current run is reported and does not fail.

## Test plan

Unit (`tests/unit/perf/StatisticsTest.cpp`): median of odd and even counts; min
and max; the spread; one sample; no samples.

Unit (`tests/unit/perf/ReportTest.cpp`): the JSON carries every field and parses;
the human summary names the benchmark and its unit.

Gate (`tests/CMakeLists.txt`, mirroring the coverage gate): `compare_baseline.py`
over checked-in fixtures — an unchanged run passes, a 30% regression fails and
names the benchmark, a regression inside the spread passes, a missing benchmark
fails, a new benchmark passes.

Not automated: the benchmark *numbers*. They are machine-dependent by nature;
what is tested is everything around them.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
