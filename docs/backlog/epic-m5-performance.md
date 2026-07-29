<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M5 — Performance

**Goal:** make the scan fast, and prove it. Recovery runs over whole disks, and the
signature scanner touches every byte of them; this milestone is that loop and nothing
else.

**Milestone:** [M5](../roadmap.md#m5--performance)

## Outcome / definition of ready-to-close

- Signature scanning meets its throughput target on the benchmark suite.
- The SIMD fast path is enabled behind a measured win, produces bit-identical output to
  the portable matcher, and falls back on a CPU that cannot run it.
- The performance gate compares two runs on metrics that survive being measured on
  different machines, and is green.
- Every number in this milestone came from the suite, not from an argument.

## Stories

| Story | Title | Size |
|-------|-------|:----:|
| [story-0501](stories/story-0501-benchmark-suite.md) | The benchmark suite, and the gate that reads it | M |
| story-0502 | One pass over the window, not one per signature | L |
| story-0503 | AVX2 prefilter behind a runtime check | M |
| story-0504 | Multi-threaded range sharding for scans | L |

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
