<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0504: Multi-threaded range sharding for scans

- Epic: [epic-m5-performance](../epic-m5-performance.md)
- Status: Backlog
- Size: L

## Goal

Scan a device's ranges concurrently and merge the results into exactly the run a
single thread would have produced. The threads are the easy half; the story is
about the other one.

## Design references

- [strategy.md](../../performance/strategy.md) — "Scanning parallelizes by range
  sharding … Bounded by I/O bandwidth, so thread count is tuned, not maximized.
  Shared state is minimized; where it exists, it is validated under TSan."
- [`src/carve/CarveMatches.cpp`](../../../src/carve/CarveMatches.cpp) —
  `resumeOffset` and the match walk. The reason this story is L.
- [`include/revenant/recovery/Arbitration.hpp`](../../../include/revenant/recovery/Arbitration.hpp)
  — the total order that makes a run reproducible, and which this story must not
  quietly start relying on to paper over a scheduling difference.
- [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md)
  — discovery indexes candidates; nothing is extracted until arbitration has
  ruled.

## Why this is not just a thread pool

Scanning is sequential in one specific way. `processMatches` walks a window's
matches in offset order, and after a candidate is accepted it **resumes past
that candidate's whole extent** — every later match falling inside a recovered
file is skipped, because a JPEG's thumbnail is not a second JPEG on the disk.

Shard the device and that rule breaks at every boundary. A file discovered near
the end of shard *N* extends into shard *N+1*, which is scanning independently
and knows nothing about it — so it reports header hits that a single-threaded
run would never have carved. Arbitration will suppress them, since they overlap
a region a better candidate already claimed, and the *winners* come out the
same. But `suppressed` is a number the manifest publishes, and "why is this file
not in the output" is a question this project promised to be able to answer. A
count that changes with core count is an answer nobody can check.

There is a second, quieter one: the candidate index records candidates in
**append order**, and with shards that order is whatever the scheduler chose.

## Scope

1. **Range sharding** of `scanRegion` across a bounded thread pool.
2. **Deterministic reconciliation** of the shards' candidates.
3. **Thread-count configuration**, defaulted rather than maximized.
4. **TSan validation** on the Linux workbench.

## Design decisions

**Parallel discovery, sequential reconciliation.** Each shard scans its range
and reports what it finds, without knowing what its neighbours found. The merge
step then walks the union in offset order and applies the *same* resume rule the
single-threaded scan applies — dropping any candidate that falls inside an
already-accepted extent — which reproduces the sequential result exactly. The
merge is O(n log n) in the number of candidates, which is nothing beside a scan
of the device: the expensive thing was reading and matching the bytes, and that
is what got parallelized.

This is deliberately not "let arbitration sort it out". Arbitration would
produce the right winners, and the wrong report.

**Shards are aligned, and a candidate belongs to the shard it starts in.**
`ScanRegion` already carries the rule that a candidate starting inside a region
is carved to its true length even when that runs past the end — it is how
per-partition scanning works today. Shards inherit it unchanged, so a file
straddling a boundary is carved once, whole, by the shard that found its header.

**The index is written by one thread.** Candidates cross to the reconciler
through a bounded queue; the durable index keeps a single writer, so its append
order stays the deterministic post-merge order and its crash-safety argument
([ADR-0008](../../architecture/adr/adr-0008-resumability-checkpointing.md)) does
not have to be re-made for concurrent appends.

**The thread count is configuration with a modest default, not
`hardware_concurrency`.** Scanning is I/O-bound before it is CPU-bound, and
oversubscribing a failing disk is worse than slow — it is a reliability
question, not just a throughput one. The default is small, the flag exists, and
the benchmark is what justifies whatever number is chosen.

**This story is allowed to end in "no".** If the matcher from
[story-0502](story-0502-one-pass-matcher.md) already saturates the I/O path,
sharding adds threads, a queue, a merge step and a class of bug the project has
never had, in exchange for nothing. That outcome is a legitimate result: it gets
measured, written down in the epic, and the code does not land. The measurement
must include at least one run against a real device, not only an in-memory
fixture — the whole question is whether the disk or the CPU is the limit.

## Acceptance criteria

- [ ] `scanRegion` shards its range across a bounded pool, on both platforms.
- [ ] The reconciled candidate sequence is identical to a single-threaded scan
      of the same device — same candidates, same order, same count.
- [ ] A run at one thread and a run at eight produce **byte-identical
      manifests**, including `suppressed`.
- [ ] A file straddling a shard boundary is discovered once, carved whole, and
      not reported twice.
- [ ] An I/O error in one shard fails the scan the way a sequential scan fails
      it, and does not leave other shards running.
- [ ] Interrupting a sharded run and resuming it produces the same result as
      interrupting and resuming a sequential one.
- [ ] Thread count is configurable, defaults to a stated modest value, and the
      default is justified by the benchmark rather than by a constant somebody
      liked.
- [ ] TSan reports no data race over the full test suite on Linux.
- [ ] `scan-throughput` and `end-to-end-hybrid` improve measurably — **or the
      story closes as measured-and-not-shipped**, with the numbers recorded.

## Test plan

Unit (`tests/unit/carve/`): the shard split covers the region exactly with no
gap and no overlap, for a region that divides evenly and one that does not, and
for a region smaller than one shard; the reconciler drops a candidate inside an
accepted extent, keeps one that merely touches its end, and is stable for
candidates at the same offset.

Integration: a synthetic device with files planted deliberately across shard
boundaries, scanned at one and at eight threads, asserting identical candidate
sequences; the same at eight threads twice, asserting identical output across
runs; a fault-injecting device failing inside one shard.

Sanitizers: the suite under TSan on the Linux workbench, in addition to the
usual ASan + UBSan.

Not automated: whether it is worth it. That is the benchmark's answer and the
story records it.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan, and under TSan on
      Linux.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `docs/performance/strategy.md` describes what sharding actually does,
      including the reconciliation step.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked; if the outcome was "not shipped", the epic says so with
      the measurement.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
