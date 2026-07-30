<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0504: Multi-threaded range sharding for scans

- Epic: [epic-m5-performance](../epic-m5-performance.md)
- Status: Done — measured, not shipped
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

## Outcome: measured, not shipped

**This story closed on its own measurement clause.** The scan was measured
against a real device before the concurrency was written, because the story's
last acceptance criterion makes the measurement the thing that decides, and
because everything above it costs a class of bug this project has never had.

### The measurement

The question is whether the disk or the CPU is the limit. A fixture of **48 GiB**
answers it on this workbench, because 48 GiB cannot sit in 31.5 GiB of RAM: the
reads have to come off the drive. `revenant-carve --dry-run`, one run each:

| Matcher | Time for 48 GiB | Rate |
|---------|----------------:|-----:|
| Default (AVX2 fast path) | 47.4 s | **1 037 MiB/s** |
| `--force-portable` | 59.6 s | 824 MiB/s |
| Ratio | | 1.26× |

The ratio is what makes this conclusive. **If the drive were the limit, a matcher
22% slower would have finished in the same time** — the reads would have set the
pace either way. Instead the whole scan slowed in proportion, which says the
operating system's read-ahead hides the drive completely and the *matcher* sets
the pace. The cold 1 037 MiB/s is within 0.4% of the 1 041 MiB/s the same binary
reaches on a fixture small enough to stay in RAM, which says the same thing
twice.

So on this machine the scan is CPU-bound, and sharding it across eight cores
would raise throughput. That is not the machine this project exists for.

### Why that is a reason not to ship it

`strategy.md` states the target: whole disks, "hundreds of gigabytes to
terabytes", on hardware that is usually already failing. A healthy spinning disk
delivers 100–150 MB/s and a failing one much less; a SATA SSD delivers around
500 MB/s. Against any of those, **one thread at 1 037 MiB/s is already between
seven and twenty times faster than the device can be read**, and the surplus is
not throughput anybody receives — it is a thread waiting on I/O.

Sharding would therefore buy nothing on the media the tool is pointed at, while
costing:

- **Threads on a dying drive.** `strategy.md` calls oversubscribing one "worse
  than slow — a reliability question, not just a throughput one". Eight
  concurrent readers on a drive with failing sectors is the one workload this
  project's own architecture warns against.
- **A determinism obligation on every future change.** A byte-identical manifest
  at one thread and at eight is not a property that is established once; it is a
  property every later change to discovery has to preserve, and `suppressed` is
  published in the manifest precisely so that "why is this file not in the
  output" can be answered.
- **Memory.** Each shard needs its own window and carve buffers — 68 MiB at the
  defaults — so eight shards is 544 MiB against the 68 MiB a sequential scan
  uses, on a tool whose stated principle is bounded, reused buffers.

The measured win exists on fast media, and it can be reclaimed later against a
real need: the seam it would need is in better shape than expected (see below).
What it cannot be justified by today is a benchmark on an NVMe drive, which is
not what a recovery tool runs against.

### What the reading of the code turned up anyway

Two facts worth keeping, because they are what a future attempt starts from:

- **`BlockDevice` has no thread-safety contract**, and it needs one before any of
  this is written. `ImageFileDevice` documents that concurrent `readAt` is safe;
  `RawDevice` is the same shape in fact (a positioned `OVERLAPPED` `ReadFile` and
  a `pread`, no shared file offset) but says nothing; `PartitionView` is offset
  arithmetic over its inner device. So the two implementations a run actually
  uses would already satisfy the requirement — the work is to state it and to
  audit it, not to build it.
- **`CachingDevice` would not.** Its `readAt` mutates an LRU list and its index,
  so it is a data race waiting for a second thread. It is not in the run path
  today, which is the only reason this is a note rather than a defect.

## Acceptance criteria

The criteria below describe the implementation, which is why they are unticked:
none of them was met, because none of them was attempted. The one that closed
the story is the last.

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
- [x] `scan-throughput` and `end-to-end-hybrid` improve measurably — **or the
      story closes as measured-and-not-shipped**, with the numbers recorded.
      *Closed the second way; the numbers are above.*

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

A story that ships no code satisfies a different list than one that does. What
applies, applies; what does not is said so rather than ticked.

- [x] The measurement is recorded here, with the fixture and the reasoning that
      makes it conclusive rather than suggestive.
- [x] `docs/performance/strategy.md` describes what is true: range sharding is
      *not* implemented, and why the measurement says so. It previously promised
      it in the future tense, which a reader would have taken for a plan.
- [x] Epic row linked, and the epic records the outcome with the numbers.
- [x] **No `CHANGELOG.md` entry.** The changelog documents changes to the
      product, and there is no change to document: no flag, no behaviour, no
      code. Recording a decision not to build something there would tell a user
      about our deliberations rather than about their tool.
- [x] Tests, sanitizers and the lint gates: nothing to run beyond what already
      passes, because no source file changed. TSan in particular is moot — there
      is no concurrency to validate.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md))
      — vacuous for a story with no new code, and recorded as such rather than
      ticked as though it had been exercised.
