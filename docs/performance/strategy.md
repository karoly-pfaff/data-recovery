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

- Baseline: a correct, portable multi-pattern matcher (Aho-Corasick-style) over streamed
  windows. This is the reference implementation and always exists.
- **SIMD fast path (M5):** an AVX2 (and where available, wider) accelerated first-stage
  filter that rejects the common "no candidate here" case cheaply, falling back to the
  exact matcher on potential hits. This is the natural home for **hand-tuned intrinsics
  or assembly**.
- **Rule:** the SIMD/assembly path must be behind a runtime feature check, produce
  bit-identical results to the portable path (a differential test asserts this), and be
  justified by a benchmark. If it is not measurably faster, it does not ship.

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
