<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0106: NTFS deleted-entry enumeration + path reconstruction

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: M

## Goal

Turn the per-record parsers of [story-0104](story-0104-ntfs-mft-record.md) and
[story-0105](story-0105-ntfs-runlist-decoder.md) into a **volume-level pass**:
walk the whole `$MFT`, and report every live, deleted, and orphaned file as a
`RecoveredEntry` carrying its reconstructed path, timestamps, and the byte
extents its content lives in. This is the filesystem half of the M1 vertical
slice — the thing carving can never do, which is give a recovered file back its
name and its place in the tree.

## Design references

- [Filesystem layer](../../architecture/filesystems.md) — the `RecoveredEntry`
  vocabulary, the enumerate-by-visitor seam, and the recoverability grading that
  is the handoff point to the carve pass.
- [Hybrid orchestration](../../architecture/hybrid-orchestration.md) — step 2b/2c:
  "enumerate live + deleted + orphaned entries" is exactly this story's output,
  and its extents are what byte accounting (story-0108) consumes.
- [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md) —
  discovery, not extraction: an entry is reported to a visitor, never written.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — bounded
  allocation and bounded walks: the parent chain is on-disk data and may be a
  cycle.

## Scope

Three responsibilities, three seams.

1. **Addressing** — `MftTable` (`include/revenant/fs/ntfs/MftTable.hpp`). The
   `$MFT` is itself a file: `open()` reads record 0, decodes its own `$DATA`
   runlist into extents, and derives the record count from its declared size.
   `readRecord(n)` then maps a record number through those extents to a device
   offset and parses what it finds there. The MFT is not assumed contiguous —
   mapping a file offset onto an extent list is its own unit
   (`src/fs/ExtentLocate.*`), because ext4 will want the same arithmetic.
2. **Path reconstruction** — `src/fs/ntfs/EntryPath.*`. A `$FILE_NAME` carries a
   parent *reference*, not a path; the path is the chain of directory records
   from the file up to record 5. Walking it is where hostile metadata bites, so
   the walk is bounded, sequence-checked, and reports whether it actually
   reached the root.
3. **Enumeration** — `enumerateEntries`
   (`include/revenant/fs/ntfs/EntryEnumeration.hpp`), building one
   `RecoveredEntry` per record (`src/fs/ntfs/EntryFromRecord.*`) and handing it
   to an `EntryVisitor`.

`RecoveredEntry` and `EntryVisitor` land in `include/revenant/fs/RecoveredEntry.hpp`
as filesystem-layer vocabulary, not NTFS types — FAT32/exFAT/ext4 (M3) report
the same shape.

**Deliberately out of scope:**

- The `FileSystem` interface sketched in `docs/architecture/filesystems.md`.
  One filesystem does not justify the abstraction (YAGNI); it arrives with the
  second one, in M3.
- `$Bitmap`. "Data clusters appear unallocated" is part of the documented
  `Valid` grade, and we cannot check it without an allocation-bitmap parser.
  Grading here is on **metadata integrity only**, and the doc is corrected to
  say so rather than the code pretending.
- `$ATTRIBUTE_LIST` (a file whose attributes spill into further records) and
  named `$DATA` streams. Such a record grades `Uncertain` with no extents, which
  routes it to the carve pass instead of producing wrong bytes.
- Where an orphan is *written* (`lost+found`-style). The entry carries
  `EntryState::kOrphaned`; naming the destination is the sink's policy
  (story-0109), not the parser's.

## Acceptance criteria

### `RecoveredEntry` / `EntryVisitor`

- [x] `RecoveredEntry` carries `path`, `sizeInBytes`, `extents`,
      `residentContent`, `timestamps`, `state`, and `recoverability`.
- [x] `path` is a volume-relative, `/`-separated UTF-8 **logical** path, not a
      host path — it becomes one only through `sanitizeOutputPath` (ADR-0009).
- [x] Content is either `extents` or `residentContent`, never both: resident
      bytes cannot be expressed as a device extent, because the on-disk copy is
      interrupted by the update-sequence fixup and is not the file's bytes.
- [x] `EntryVisitor::onEntry` is the only output seam — nothing is extracted.

### `MftTable`

- [x] `MftTable::open(BlockDevice&, const NtfsGeometry&)` reads record 0, and
      requires it to be a parseable `FILE` record with a **non-resident**
      `$DATA` — a resident `$MFT` is `kInvalidArgument`, not a guess.
- [x] The MFT's own runlist is decoded and mapped with the story-0105 units;
      `recordCount()` is its declared size divided by `bytesPerMftRecord`.
- [x] `readRecord(n)` rejects `n >= recordCount()` with `kOutOfRange`.
- [x] `readRecord(n)` locates the record through the MFT's extent list, so a
      **fragmented** `$MFT` is read correctly.
- [x] A record that straddles two extents is `kInvalidArgument`, not a partial
      read stitched together from the wrong place.
- [x] A short device read is `kOutOfRange`; the read is never used partially.

### Path reconstruction

- [x] A file's own name is its `$FILE_NAME`; when a record carries several, the
      Win32 name wins over the DOS 8.3 alias.
- [x] The path is built by walking parent references to record 5 (the root),
      joined with `/`, root itself contributing no segment.
- [x] A parent that cannot be read, is not a directory, or whose record
      sequence does not match the reference's sequence (the slot was reused)
      ends the walk — the entry is orphaned, and keeps the path it had.
- [x] The walk is bounded by `kMaxPathDepth`; a parent cycle terminates and
      reports orphaned rather than hanging (ADR-0009).

### Enumeration

- [x] `enumerateEntries(const MftTable&, EntryVisitor&)` walks records from
      `kFirstUserRecord` (16 — NTFS reserves 0–15 for metadata files) to the end
      of the table, and returns `EnumerationStats`.
- [x] Live, deleted, and orphaned files are all reported; `EntryState` tells
      them apart. Directory records and records with no `$DATA` are not entries.
- [x] A record slot that does not parse (no `FILE` signature, damaged header)
      is **skipped**, not an error: that region is the carve pass's territory.
- [x] A device read fault (`kIoFailure`) aborts the enumeration as a typed
      error — a disk we cannot read is not a disk with no files on it.
- [x] `recoverability` is `kValid` only when the record parsed cleanly, the path
      reached the root, and the content was resolvable; anything else is
      `kUncertain`, which is what routes the region to carving.
- [x] A `$DATA` whose runlist will not map (sparse, out of volume) yields an
      entry with no extents and `kUncertain` — never approximated bytes.
- [x] A libFuzzer target `NtfsEnumerateFuzz` drives `MftTable::open` +
      `enumerateEntries` over arbitrary bytes as a device and must never crash
      or hang.

## Test plan

Unit (`tests/unit/fs/ExtentLocateTest.cpp`): offset inside the first/second/last
extent; the exact first and last byte of an extent; an offset past the end; a
length straddling an extent boundary; an empty extent list; a length that
overflows the offset.

Unit (`tests/unit/fs/ntfs/MftTableTest.cpp`), over the story-0118 fixture image
mounted as an `InMemoryDevice`: `open` yields the layout's record count; record 0
reads back as `$MFT`; a known record reads back with its fixture name; an
out-of-range record number is `kOutOfRange`; an image whose record 0 is blanked
fails `open`; an image whose `$MFT` `$DATA` is resident fails `open`.

Unit (`tests/unit/fs/ntfs/EntryPathTest.cpp`): a file directly under the root
resolves to a bare name; a file one directory deep resolves to `photos/keep.jpg`;
the orphan's missing parent reports not-reached-root with the bare name; a
parent record whose sequence has been bumped in the image bytes reports
not-reached-root; a record whose parent link points at itself terminates.

Unit (`tests/unit/fs/ntfs/EntryEnumerationTest.cpp`): the fixture volume yields
exactly the four user files with their expected states and paths; the `$MFT`,
the root, and the `photos` directory are not entries; a blanked record slot is
skipped rather than failing the walk; a device that faults on read propagates
`kIoFailure`.

Integration (`tests/integration/NtfsEnumerationTest.cpp`): the fixture image
through `ImageFileDevice` → `parseBootSector` → `MftTable` → `enumerateEntries`,
asserting each entry's path, state, grade, timestamps, and — by reading its
reported extents back off the device — that its content is byte-identical to the
fixture's. That last assertion is the story's real claim: an entry's extents are
where the file's bytes actually are.

Fuzz (`tests/fuzz/NtfsEnumerateFuzz.cpp`): arbitrary bytes as a device, with the
boot sector parsed from the same bytes when it parses and a fixed synthetic
geometry otherwise. Seed corpus: the fixture image's MFT region.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/filesystems.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
