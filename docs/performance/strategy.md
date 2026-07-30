<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Performance Strategy

Recovery runs over whole disks — hundreds of gigabytes to terabytes. Performance is a
feature, but **correctness and precision always come first**: we never trade a false
positive for speed. Every optimization is justified by a benchmark, never by intuition.

## Principles

- **Measure first.** No optimization lands without a benchmark showing a real win on the
  [benchmark suite](benchmarks.md). "Faster in theory" is not a reason to add complexity.
  The suite measures the shipped binaries from outside the process, so what it reports is
  what a user would experience — and it refuses to report numbers from a build that was
  not optimized.
- **Correctness is not negotiable.** An optimization that changes recovery output is a
  bug, not a speedup. Golden-file tests guard this.
- **Streaming, always.** No layer loads a whole device or partition into memory. Data is
  read in bounded windows and released.
- **I/O is usually the bottleneck.** Large sequential reads via `CachingDevice` matter
  more than micro-optimized parsing for most workloads. Optimize the dominant cost.

## Where the time goes (expected)

1. **Device I/O** — mitigated by large aligned reads and read-ahead caching.
2. **Signature scanning** — a multi-pattern search over every byte of the device. This
   is the hottest CPU loop and the primary optimization target.
3. **Per-candidate validation** — bounded by the number of header hits, usually small
   relative to scanning.

## The signature scanner

- Baseline: a correct, portable multi-pattern matcher over streamed windows, in **one
  pass** regardless of how many signatures are registered (story-0502). Every window
  position asks a 256-entry table, indexed by the byte there, whether any signature could
  begin with it — one load and one test, which is the answer for almost every byte of
  almost every device — and hands the rare survivor to an exact comparison. Adding an
  eighth format therefore costs the scan nothing per byte, where the matcher this replaced
  made one full `std::ranges::search` pass per signature per window.
  This is the reference implementation and always exists. The matcher it replaced is kept
  in `tests/support/ReferenceMatcher.cpp` as the oracle a differential test asserts
  against: for randomized windows over a seeded generator, both must produce an identical
  `Match` sequence, offsets, carvers and order alike.
- **SIMD fast path (M5, story-0503):** an AVX2 first-stage filter that rejects the common
  "no candidate here" case 32 positions at a time, handing every survivor to the same
  exact comparison the portable path uses. It vectorizes exactly the one-load test above,
  so the two paths stay one algorithm with two implementations of one step — which is what
  makes identical output a claim that can be met rather than hoped for. The filter is
  *conservative in one direction only*: it may pass a byte no signature starts with,
  because a high nibble shares a mask bit with that nibble plus eight, and it may never
  drop one. Passing too many costs a comparison; dropping one would change what a scan
  finds.
  It lives in a single translation unit compiled with AVX2 alone, behind a `CPUID` check
  performed once when the signature table is built. `--force-portable` turns it off on
  both frontends: the escape hatch for a CPU where the fast path misbehaves.
  **Measured at 1.22× on the Windows workbench** — the same fixture, one machine, back to
  back, which is why `scan-simd-vs-portable` is a ratio.
- **Rule:** the SIMD/assembly path must be behind a runtime feature check, produce
  bit-identical results to the portable path (a differential test asserts this over
  randomized windows, against three implementations at once), and be justified by a
  benchmark. If it is not measurably faster, it does not ship. story-0503's first two
  attempts *were* slower and were rewritten rather than argued for; the numbers are in the
  story.

## Parallelism

- Scanning parallelizes by **range sharding**: the device is split into aligned
  segments scanned concurrently, results merged. Bounded by I/O bandwidth, so thread
  count is tuned, not maximized.
- Shared state is minimized; where it exists, it is validated under TSan.
- Filesystem parsing is largely sequential (metadata dependencies) and is not forced
  into parallelism for its own sake (YAGNI).

## Memory

- Fixed-size, reused buffers; no per-candidate large allocations in the hot path.
- Bounded caches with explicit eviction (`CachingDevice`), sized by configuration, not
  unbounded growth.

## Non-goals (for now)

- GPU offload, memory-mapping whole devices, and lock-free exotica are out of scope
  until a benchmark demonstrates a need. We do not build speculative performance
  machinery (YAGNI).
