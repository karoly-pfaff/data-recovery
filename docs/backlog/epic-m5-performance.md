<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M5 — Performance

**Goal:** make the scan fast, and prove it. Recovery runs over whole disks, and the
signature scanner touches every byte of them; this milestone is that loop and nothing
else.

**Milestone:** [M5](../roadmap.md#m5--performance)

## Outcome / definition of ready-to-close

All four met. What the milestone measured:

- **Signature scanning is 6.6× faster on the CI runner** — 329 → 2 186 MiB/s — and
  executes a fifth of the instructions it did. Two changes got there: one pass over the
  window instead of one per signature (story-0502, +275%), then a vectorized reject behind
  a runtime check (story-0503, a further +77%).
- **The SIMD fast path shipped on a measured win**, 1.59× against the portable matcher on
  the runner and 1.22× on the workbench, proved bit-identical by a differential test
  against three implementations, behind a `CPUID` check made once, with `--force-portable`
  for the CPU where it misbehaves.
- **The performance gate compares two runs** at thresholds the suite measured rather than
  thresholds anybody liked, and is green. It also caught its own design flaw in the act:
  see below.
- **Every number came from the suite.** Three times it said something other than what was
  expected, and each time the story records what it said rather than what was hoped for:
  story-0502's first attempt was slower than the seven-pass matcher it replaced,
  story-0503's first attempt was slower than the portable path it was meant to beat, and
  story-0504's premise turned out to hold only on hardware this tool is not for.

**story-0504 closed as measured-and-not-shipped.** Scanning 48 GiB off a drive — more than
this machine can cache — runs at 1 037 MiB/s, and the *slower* portable matcher over the
same fixture takes proportionally longer, which proves the CPU and not the drive sets the
pace. Sharding would therefore raise throughput on an NVMe SSD. It would raise nothing on
a failing disk at 50–150 MB/s, where one thread already delivers seven to twenty times
what the device can supply, and where `strategy.md` calls oversubscribing "worse than
slow — a reliability question". The story has the numbers and the reasoning; no
concurrency code landed.

**The gate found a flaw in itself.** `carve-validate` ran 6.9× slower on a runner while
executing 16% *fewer* instructions than the baseline, twice in a row, with a 0.4%
within-run spread, on code that measured unchanged on the workbench — that case re-reads
the carve bound per candidate, so it measures the host's memory bandwidth more than the
program. A rate drop now has to be corroborated by the instruction count before it counts
as a regression, and both observations are checked in as gate fixtures.

## Stories

| Story | Title | Size |
|-------|-------|:----:|
| [story-0501](stories/story-0501-benchmark-suite.md) | The benchmark suite, and the gate that reads it | M |
| [story-0502](stories/story-0502-one-pass-matcher.md) | One pass over the window, not one per signature | L |
| [story-0503](stories/story-0503-avx2-prefilter.md) | AVX2 prefilter behind a runtime check | M |
| [story-0504](stories/story-0504-range-sharding.md) | Multi-threaded range sharding for scans — *measured, not shipped* | L |

story-0501 was numbered **story-0050** during its first, reverted implementation
(`e65ea08`, before the `story-MMNN` scheme existed). That number is retired rather than
reused, so a reader who meets it in `git log` is not sent to unrelated work.

## What each story is

**story-0501 — the benchmark suite.** It goes first because the rule it enforces — no
optimization without a measurement — governs everything below it, and because the
measurement it replaces did not work. Built once as a C++ binary calling `librevenant`
in-process, which put it in the wrong layer: the metrics actually worth gating, peak
memory and instruction count, are properties of a *process*, and reading them from inside
means platform code that would exist only for the benchmark. It could not see I/O either,
though [strategy.md](../performance/strategy.md) calls I/O the dominant cost. Every case
turns out to be measurable from outside by driving the shipped binaries, so the harness
becomes Python and the C++ half is reverted. Wall-clock stops being a gate, because two
GitHub runners are two different machines; what gets gated is what is deterministic
across them. It also brings the Linux workbench with it, since `valgrind` is Linux-only:
provisioning a WSL Debian with the pinned toolchain is a documented `install.md`
procedure, not a story of its own.

**story-0502 — one pass over the window.** [strategy.md](../performance/strategy.md)
describes the baseline matcher as "Aho-Corasick-style". It is not: `WindowMatch.cpp` runs
one `std::ranges::search` per signature per carver over the whole window — seven full
passes over every byte of the device, with a generic search each time. The largest
available win in the scan loop is therefore not SIMD but the algorithm, and it is
entirely portable. This story replaces the nested searches with a single pass, and builds
the differential harness the SIMD story will need anyway: the old matcher becomes the
reference implementation, and the new one must produce a bit-identical `Match` list over
randomized windows. Acceptance is measured, not argued.

**story-0503 — the AVX2 prefilter.** Only now is there something worth accelerating: a
cheap first-stage reject for the common "no candidate here" window, behind a runtime
CPUID check, falling back to the portable matcher. Rules from
[strategy.md](../performance/strategy.md) apply unchanged — bit-identical output proved by
the differential test, a benchmark win, or it does not ship. This is where
`scan-simd-vs-portable`, the fifth benchmark [benchmarks.md](../performance/benchmarks.md)
specifies, finally has two implementations to compare — and it is the one time-based
measurement worth gating, because a ratio taken on one machine divides that machine's
speed out. It brings `--force-portable` with it: the switch the benchmark needs is also
the escape hatch an operator needs on a CPU where the fast path misbehaves.

**story-0504 — range sharding.** The device is split into aligned segments scanned
concurrently. The hard part is not the threads: it is that candidates found in different
shards must arbitrate into the same result no matter how the scheduler interleaved them.
A recovery tool whose output depends on core count is a recovery tool nobody can verify,
so the acceptance criterion is a byte-identical manifest at one thread and at eight. TSan
validation runs on the Linux workbench; MSVC has no thread sanitizer. The story may also
end in a measured "no" — if the improved matcher already saturates I/O, threads add risk
and no throughput, and that outcome gets documented rather than overridden.

## Notes

- Performance work is **measurement-gated**: no hand-tuned or assembly code lands
  without a benchmark proving the win. See [strategy.md](../performance/strategy.md).
  story-0502 before story-0503 is that rule applied to the ordering itself — an
  intrinsics path written against a seven-pass matcher would be measured against the
  wrong baseline and would flatter itself. story-0501 before both is the same rule
  applied one level further down: a measurement that cannot tell a win from runner noise
  gates nothing.
- **CI must not grow another C++ build.** Nine of the ten jobs already compile the tree
  from scratch. story-0501 introduces a `build-release` job whose artifact it consumes
  instead of building its own, and [M7](epic-m7-release.md)'s packaging consumes the same
  one — so the two milestones together add one build, not three.
- **The repository is public** as of M5, which returned the Actions minutes the free plan
  had exhausted. Two consequences: a **self-hosted runner is now ruled out**, because a
  fork's pull request would execute on it; and CodeQL became free and is **not taken
  here** — a performance milestone is the wrong place to add a correctness gate. It is
  carried in [M6](epic-m6-loose-ends.md).
