<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0501: The benchmark suite, and the gate that reads it

- Epic: [epic-m5-performance](../epic-m5-performance.md)
- Status: Done
- Size: M

## Goal

Make "faster" a thing that can be shown rather than claimed. The performance
strategy forbids any optimization that is not justified by a benchmark, so the
benchmark has to exist before the first optimization does — this is that
benchmark, plus the comparison that turns a pair of runs into a pass or a fail.

## This story was built once and reverted

Commit `e65ea08` implemented it as a C++ binary, `revenant-bench`, calling
`librevenant` in-process. That was the wrong layer, and the commit is reverted
rather than patched. The reasoning is kept here because it is the reason this
version looks the way it does:

- **The metrics worth gating are process-level.** Peak memory and instruction
  count are properties of a program, not of a function call. Reading peak RSS
  from inside means `GetProcessMemoryInfo` on Windows and `/proc/self/status` on
  Linux — platform code existing solely for a benchmark, in a tree where
  `AGENTS.md` confines platform code to `core/io/`. Instruction count cannot be
  read from inside at all.
- **It could not see I/O.** Every fixture was an `InMemoryDevice`, so the cost
  [strategy.md](../../performance/strategy.md) calls dominant was not measured.
- **It could not see memory.** "Streaming, always" and "fixed-size, reused
  buffers" are stated principles that nothing enforced.
- **It cost a build everywhere.** `revenant-bench` was built by every preset, not
  only `release`, so 727 lines of C++ and 144 of tests were compiled in both
  build-test legs and the coverage build, and walked by all four clang-tidy
  shards — plus a whole `release` build in a CI job of its own.
- **Nothing needed the inside.** Each case turned out to be measurable by
  driving the shipped binaries; see the table below.

What survives untouched is the *discipline*: the warm-up, the median as the
headline, the spread as a veto, work units travelling with the case, no baseline
file in the repository, and `compare_baseline.py` itself, whose input is JSON and
therefore does not care who produced it.

## Design references

- [performance/strategy.md](../../performance/strategy.md) — "Measure first. No
  optimization lands without a benchmark showing a real win", and "I/O is usually
  the bottleneck", which is the line the first attempt could not honour.
- [performance/benchmarks.md](../../performance/benchmarks.md) — the suite's
  specification. This story implements it and rewrites it around process-level
  measurement.
- [`tools/lint/check_coverage.py`](../../../tools/lint/check_coverage.py) — the
  precedent every gate here follows: a pure function over structured data, driven
  in `ctest` by checked-in fixtures, so the *gate itself* is tested.

## Scope

1. **A Python harness** in `tools/perf/`: run a case as a subprocess, repeat it,
   and report wall-clock, peak RSS and work units. No C++.
2. **Four cases**, each driving a shipped binary over a fixture on disk:

   | Case | Driven by | Work unit |
   |------|-----------|-----------|
   | `scan-throughput` | `revenant-carve --dry-run` over an image with no filesystem | MiB scanned |
   | `carve-validate` | `revenant-carve --dry-run` over a high-header-density image | candidates |
   | `ntfs-enumerate` | `revenant-undelete --fs-only --dry-run` over an NTFS image | entries |
   | `end-to-end-hybrid` | `revenant-undelete --hybrid --dry-run` over a partitioned disk | MiB |

   The fifth case the spec lists, `scan-simd-vs-portable`, needs a SIMD path to
   compare against and belongs to story-0503.
3. **Instruction counts** under `valgrind --tool=cachegrind --cache-sim=no`,
   where valgrind exists.
4. **`tools/perf/compare_baseline.py`** — the regression gate: two result files
   in, a verdict out, at stated thresholds.
5. **CI** — a `build-release` job publishing the binaries as an artifact, and a
   `benchmarks` job that consumes it and installs no toolchain at all.
6. **Tests** — for the statistics, for the JSON, and for the gate's own verdict
   logic against checked-in fixtures.

## Design decisions

**The measurement lives outside the process.** The harness starts a binary and
watches it. That is what makes peak RSS free (the OS reports it), instruction
count possible (valgrind wraps the process), and real I/O visible (the fixture is
a file). It is also what a user experiences, which is the number that ultimately
matters for a tool pointed at a failing disk. Process startup is around ten
milliseconds against multi-second runs — noise, not a correction to apply.

**The work units are already printed.** `RunSummary` reports `regions scanned`,
`carve candidates` and `filesystem entries` in a greppable form, so the harness
reads what the run says it did rather than assuming. A rate computed from a guess
about what the case processed would be a guess about the rate.

**Fixtures carry the work; there is no inner repeat loop.** A case that finishes
near the clock's own noise is fixed by giving it more to do — more MFT records,
more planted headers — not by running it 2 000 times inside one timing. The
fixtures are generated by `tools/imagegen` into a working directory and are not
committed.

**Wall-clock is not a gate.** Two runs of identical code on two GitHub-hosted
runners are two different machines, often different CPU models, and can differ by
far more than ten percent. The within-run spread is around 1.4% and cannot absorb
that. So the thresholds split by what is actually deterministic:

- **Gated tightly:** peak RSS and instruction count, both repeatable across
  machines to a fraction of a percent.
- **Gated loosely, or reported only:** absolute rates, which catch the accidental
  quadratic and nothing finer.
- **The one time-based gate worth having** is a ratio measured on one machine in
  one sitting — the SIMD-versus-portable speedup of
  story-0503. A ratio divides the machine out.

**The median is the headline and the spread is the veto.** A single fast run
proves nothing on a machine with other work on it. The report carries min, max
and median, and the comparison refuses to call a change a regression when the
baseline's own spread already covers it — otherwise the gate cries wolf on every
noisy runner and gets ignored, which is worse than not having it.

**No baseline file is checked in.** An absolute number captured on one machine is
meaningless on another, and a repository is the wrong place to keep a machine's
measurement. `compare_baseline.py` takes *two* result files, which is what lets
CI compare a pull request's run against `main`'s run on the same runner class,
and what lets a developer compare two of their own runs.

**One release build, several consumers.** The benchmarks do not build anything:
`build-release` compiles once and publishes the binaries, and both this job and
the packaging in story-0801 consume that artifact.
Nine of CI's ten jobs already compile the tree from scratch; the milestone must
not end with eleven.

**`end-to-end-hybrid` measures discovery, not extraction.** `--dry-run` runs the
whole engine — the filesystem walk over every partition and the carve scan over
what it did not claim — and stops before writing recovered files, because writing
them measures the *destination's* disk and swamps the thing under test.

**If something ever genuinely needs measuring from the inside, it goes into the
product.** Not into a tool. The shape that would take is a run reporting its own
phase timings and throughput, which is an operator feature before it is a
benchmark feature: somebody watching a two-terabyte recovery needs the rate and
the time remaining. No clock exists in `src/cli/` today and this story does not
add one; it records where one would belong.

## Acceptance criteria

- [x] `tools/perf/` contains the Python harness and `compare_baseline.py`, and no
      C++ and no build target of its own.
- [x] The harness runs every case, or only those matching `--filter`.
- [x] It generates its fixtures via `tools/imagegen` into a working directory,
      and does not commit them.
- [x] `--json <path>` writes one object per case: name, unit, median, min, max,
      spread, peak RSS, work units and rate.
- [x] Instruction count is reported for at least `scan-throughput` under
      `valgrind --tool=cachegrind --cache-sim=no`, and is *absent* rather than
      faked where valgrind is unavailable.
- [x] A case whose subprocess exits non-zero is a failure, not a zero
      measurement.
- [x] The human summary names each case, its rate and its spread.
- [x] The harness refuses to report numbers from a binary built without
      optimization, rather than printing a figure someone might paste into a pull
      request.
- [x] `compare_baseline.py` exits 0 when nothing regressed beyond the threshold,
      and non-zero naming the case when something did.
- [x] It gates peak RSS and instruction count at a stated tight threshold, and
      wall-clock rates at a stated loose one.
- [x] A regression inside the baseline's own spread is not called a regression.
- [x] A case present in the baseline but missing from the current run is a
      failure, not a silent pass.
- [x] A case that is new in the current run is reported and does not fail.
- [x] CI: `build-release` publishes the binaries; `benchmarks` consumes them and
      installs no compiler, no vcpkg and no CMake.

## Test plan

Unit (`tests/unit/lint/`, alongside the other gate tests): the statistics —
median of odd and even counts, min, max, the spread, one sample, no samples; the
JSON carries every field `compare_baseline.py` reads; a non-zero subprocess exit
raises rather than measuring.

Gate (`tests/CMakeLists.txt`, mirroring the coverage gate): `compare_baseline.py`
over checked-in fixtures — an unchanged run passes, a 30% rate regression fails
and names the case, a regression inside the spread passes, a peak-RSS regression
at the tight threshold fails, an instruction-count regression at the tight
threshold fails, a missing case fails, a new case passes.

Manual, recorded on completion: one run of the suite on the workbench with its
numbers, and two consecutive valgrind invocations showing the instruction count
repeating to within the claimed tolerance.

## What it measured

Two runs, one per platform, both at five repetitions. They are *not* comparable
with each other — that is the whole point of what the gate does and does not
compare — but each is comparable with the next run on the same platform.

Windows workbench (MSVC RelWithDebInfo, no valgrind, so no instruction count):

| Case | Rate | Median | Spread | Peak RSS |
|------|------|--------|:------:|---------:|
| `scan-throughput` | 643.4 MiB/s | 0.199 s | 2.6% | 72.5 MiB |
| `carve-validate` | 1 627 candidates/s | 1.259 s | 1.3% | 72.6 MiB |
| `ntfs-enumerate` | 3 307 entries/s | 2.468 s | 1.5% | 15.3 MiB |
| `end-to-end-hybrid` | 38.0 MiB/s | 0.263 s | 3.0% | 72.9 MiB |

CI, `ubuntu-latest` (GCC RelWithDebInfo):

| Case | Rate | Median | Spread | Peak RSS | Instructions |
|------|------|--------|:------:|---------:|-------------:|
| `scan-throughput` | 327.0 MiB/s | 0.391 s | 3.2% | 72.1 MiB | 6 772 927 167 |
| `carve-validate` | 3 936 candidates/s | 0.520 s | 1.3% | 72.1 MiB | 672 453 701 |
| `ntfs-enumerate` | 26 268 entries/s | 0.311 s | 1.7% | 17.3 MiB | 619 911 016 |
| `end-to-end-hybrid` | 106.5 MiB/s | 0.094 s | 0.8% | 72.1 MiB | 1 510 993 584 |

Two observations worth carrying into the rest of the milestone:

- **`scan-throughput` is twice as fast on Windows**, on slower hardware. The
  MSVC standard library vectorizes `std::ranges::search` over byte ranges and
  libstdc++ does not, so the seven-passes-per-window matcher costs Linux far
  more — which is where [story-0502](story-0502-one-pass-matcher.md)'s win will
  show up, and why it must be measured there.
- **`carve-validate` is quadratic**, and the suite says so plainly: doubling the
  corpus from 4 to 8 MiB took the Windows run from 0.38 s to 1.26 s. Each
  candidate re-reads up to `kDefaultMaxCarveBytes` from the device, so cost
  grows with the square of a header-dense region. Nothing in M5 promises to fix
  that; the number is now on the record for whoever picks it up.

### What two consecutive CI runs said, and what it changed

The thresholds this story shipped with are not the ones it was designed with,
because the first thing the suite measured was itself. Two `benchmarks` runs of
near-identical code, on two `ubuntu-latest` machines:

| Case | Instruction count | Peak RSS | Rate |
|------|------------------:|---------:|-----:|
| `scan-throughput` | −0.000% | +0.01% | +0.6% |
| `carve-validate` | −0.003% | −0.03% | −2.0% |
| `ntfs-enumerate` | −0.062% | **+5.28%** | +0.2% |
| `end-to-end-hybrid` | +0.000% | +0.06% | **−22.5%** |

- **The instruction count is as repeatable as claimed** — 0.06% at worst, across
  two machines. It is the metric that actually holds the line, and 5% leaves it
  eighty times its own noise.
- **Peak RSS at 5% was too tight, and the gate said so by going red.**
  `ntfs-enumerate` is the only case whose footprint is small (18 MiB against the
  others' 72 MiB, which is mostly the fixed carve and window buffers), so one
  allocator arena is 5% of it. The threshold is 10%, which is twice the observed
  noise and still an order of magnitude below anything a leak or a mis-sized
  buffer would do. **The 5.3% observation is checked in as a gate fixture**, so
  raising it again has to argue with a test.
- **A rate can move 22.5% between two runners** with a within-run spread of
  0.8%. That is the whole reason rates are gated at 25% and no finer, and it
  says the loose threshold is about right rather than merely lax.

The thresholds also lost their command-line overrides in the process. A
threshold that can be passed on the command line is a threshold somebody will
pass on the command line to turn a red run green.

Not automated: the benchmark *numbers* themselves. They are machine-dependent by
nature; what is tested is everything around them.

## Definition of Done

- [x] `e65ea08` reverted; no `revenant-bench`, `revenant_perf` or
      `tests/unit/perf/` remains, and no preset builds them.
- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, the duplication gate and the file-length guard
      clean.
- [x] `CHANGELOG.md`'s `[Unreleased]` describes the suite as it ships — not an
      addition followed by a removal, since `v0.2.0` predates `e65ea08` and no
      release ever carried it.
- [x] `docs/performance/benchmarks.md` and `strategy.md` agree with what is
      measured and what is gated.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md))
      completed.
