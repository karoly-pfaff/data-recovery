<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0019: File-backed candidate index + confidence arbitration

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: L

## Goal

Separate discovery from extraction for real. Both recovery sources now produce
findings ([story-0015](story-0015-hybrid-orchestrator.md)), but nothing decides
between two explanations of the same bytes. This story gives them one durable
place to land and one rule for resolving them: the higher-confidence candidate
wins the region, and only winners are ever materialized — the behaviour
[ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md)
was written for, and the reason a photos-and-video drive stops producing a
multi-gigabyte "SWF" alongside the real files.

## Design references

- [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md) —
  discover → arbitrate → extract, over a **file-backed** index, because this
  has to scale to terabyte devices.
- [ADR-0008](../../architecture/adr/adr-0008-resumability-checkpointing.md) —
  the index is durable, append-only, and validated on reload; a corrupt or
  incompatible one is rejected rather than misused. Checkpoint/resume itself is
  [story-0064](../epic-m1-vertical-slice.md).
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — every count in
  the on-disk format is untrusted on the way back in.
- [Recovery output](../../architecture/recovery-output.md) — the index is what
  the manifest and the dry-run preview will later be derived from, which is why
  it carries provenance and timestamps rather than just regions.

## Scope

1. **Vocabulary** — `Candidate` and `CandidateSource`
   (`include/revenant/recovery/Candidate.hpp`). One hypothesis about some
   bytes: where they are, how far they are trusted, what they would be called,
   and which source is claiming them.
2. **The index** — `CandidateIndex`. Two append-only files in a session
   directory: fixed-size records (`candidates.idx`) and a variable-length blob
   (`candidates.dat`) holding names, extent lists, and resident content. The
   blob is written *before* its record, so a record can never refer to bytes
   that are not on disk — crash consistency by construction rather than by
   fsync ordering hope.
3. **Arbitration** — `arbitrate()`. Candidates are considered in confidence
   order; one whose extents are all still free wins and claims them, and one
   overlapping an already-claimed region loses.
4. **The interval set** — `RegionSet`, the "which bytes are spoken for"
   primitive underneath both arbitration and story-0015's `ByteAccounting`,
   which is refactored onto it. Two implementations of the same fusing rule was
   one too many.
5. **Adapters** — `IndexingEntryVisitor` / `IndexingCandidateVisitor`, so a
   hybrid run's two streams land in one index without the orchestrator learning
   what an index is.

## Design decisions

**Discovery is unbounded; arbitration is bounded.** Appending never grows
memory — that is what makes the index file-backed and what lets a terabyte scan
finish. Arbitration loads a bounded working set (`kMaxIndexedCandidates`) and
refuses to exceed it, because the alternative is an unbounded allocation driven
by device contents (ADR-0009). External sorting, if a real device ever needs
it, is a later story rather than speculative machinery here.

**A candidate wins whole or not at all.** A carve candidate that overlaps a
named file only partly is not a better explanation of the rest; it is a worse
explanation of the same region. Accepting it piecewise would emit fragments,
which is the failure mode arbitration exists to remove.

**A named entry beats a carve of the same bytes outright, ahead of
confidence.** The two confidence scales do not measure the same thing: a
carver's verdict grades the structure of the bytes in front of it, while a
filesystem entry knows the file's name, its timestamps, and — decisively —
which runs its content is spread across. A carve beginning at a fragmented
file's first run would hand back garbage however perfect it looked, so ranking
a structurally confident carve above an uncertain named entry would be actively
wrong, not merely a lost name. This is what ADR-0006 means by filesystem
entries entering the index "as primaries". After source comes confidence, then
the earlier offset, then the larger candidate; the last two are arbitrary, and
*stating* them is what makes a run reproducible.

**A candidate with no extents always wins.** Resident content occupies no
device region it could lose, and it is carried in the blob so extraction never
has to go back to the source for it.

## Acceptance criteria

### `Candidate` / `CandidateIndex`

- [x] `Candidate` carries `name`, `extents`, `residentContent`, `timestamps`,
      `confidence`, and `source`.
- [x] `CandidateIndex::create(directory)` starts an empty index; `append()`
      writes the blob first and the fixed record second.
- [x] `readIndex(directory)` returns every appended candidate, in append order.
- [x] The record file carries a magic and a version; a foreign or
      version-mismatched index is `kInvalidArgument`, not misread.
- [x] A torn tail record (the file's record area is not a whole multiple of the
      record size) is dropped and counted, never half-interpreted.
- [x] A record whose blob range runs past the blob file is dropped and counted.
- [x] Name length, extent count, and resident length are each bounded on the
      way in (ADR-0009); a record past a bound is dropped and counted.
- [x] `readIndex` refuses an index holding more than `kMaxIndexedCandidates`
      records with a typed `kOutOfRange`, rather than allocating for it.
- [x] Every on-disk integer is explicitly little-endian, so an index written on
      one machine reads back on another.

### `RegionSet`

- [x] `add` fuses overlapping and touching regions; the set stays in offset
      order and proportional to distinct regions.
- [x] `overlaps(extent)` is true when any byte of `extent` is already claimed,
      false for a region merely touching one end-to-end.
- [x] `gaps(deviceSize)` is the complement, clipped to the device.
- [x] `ByteAccounting` is refactored onto it with its behaviour unchanged (its
      tests are untouched).

### `arbitrate`

- [x] A candidate whose extents are all free wins and claims them.
- [x] A lower-confidence candidate overlapping a winner loses, and is reported
      as suppressed rather than silently dropped.
- [x] A candidate wins whole or not at all: one overlapping extent loses it.
- [x] A filesystem entry beats a carve of the same bytes whatever the two
      confidences say; after source come confidence, the lower offset, and the
      larger size.
- [x] A candidate with no extents always wins.
- [x] Two candidates that do not overlap both win, whatever their confidence.
- [x] `kRejected` candidates never win.
- [x] The result reports winners in device order plus how many were suppressed.

## Test plan

Unit (`tests/unit/recovery/RegionSetTest.cpp`): overlap true/false at both
edges; touching regions fuse but do not overlap; containment; the complement
against a device size; an empty set.

Unit (`tests/unit/recovery/CandidateIndexTest.cpp`): a round trip of one
candidate with every field populated; several candidates in append order; a
carve candidate with no name and one extent; a resident candidate with no
extents; an empty index; a truncated record file (torn tail dropped and
counted); a record pointing past the blob; a bad magic; a bad version; a
record over the name/extent/resident bounds; a record count past the cap.

Unit (`tests/unit/recovery/ArbitrationTest.cpp`): disjoint candidates all win;
a carve candidate under a filesystem entry loses; the same in reverse append
order (the outcome must not depend on discovery order); partial overlap loses
the whole candidate; a named entry wins over a carve at equal confidence *and*
when the carve is the more confident of the two; the lower offset breaks a
same-source tie; a no-extent candidate always wins; rejected candidates never
win; the suppressed count.

Integration (`tests/integration/ArbitratedRecoveryTest.cpp`): a full hybrid run
over the story-0065 fixture image, indexed through the adapters and arbitrated.
The four named files win, the JPEG in unallocated space wins, and the carve
candidate covering a named file's own bytes is suppressed — the ADR-0006
behaviour, demonstrated end to end on a real image rather than asserted in a
unit test.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/hybrid-orchestration.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
