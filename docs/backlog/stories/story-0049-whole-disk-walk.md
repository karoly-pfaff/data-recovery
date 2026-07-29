<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0049: A whole disk walked as all of its partitions

- Epic: [epic-m4-devices-partitions](../epic-m4-devices-partitions.md)
- Status: Done
- Size: M

## Goal

Point a run at a whole disk and have it recover from *every* volume on it,
without being told which. [story-0045](story-0045-partition-selection.md) let an
operator name one partition; this is the case where they should not have to —
the epic's "target a whole physical disk and iterate its partitions".

## Design references

- [story-0045](story-0045-partition-selection.md) — `volume::readPartitionTable`
  and `PartitionView`, the two things this walks with.
- [Hybrid orchestration](../../architecture/hybrid-orchestration.md) — the
  filesystem pass accounts for what it found, and the carve pass searches what is
  left. Both halves depend on the accounting being in *device* coordinates.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — two volumes on
  one disk very often hold the same path, and both are about to be written into
  one destination.

## Scope

1. **`enumerateDisk`** (`src/recovery/PartitionedWalk.hpp`) — mounts and walks
   every partition a device's table describes, and falls back to walking the
   device whole when it carries no table.
2. **Coordinate translation** — every extent a partition reports is restated as
   a device offset before it leaves the pass.
3. **Path qualification** — every path is placed under the partition it came
   from, so two volumes cannot collide in the destination.
4. The filesystem pass calls it instead of `enumerateVolume`.

## Design decisions

**Extents are translated on the way out, not on the way in.** A filesystem
mounted on a `PartitionView` reports offsets relative to that view, because that
is the device it was handed and it has no idea it is a window. Everything
downstream — the byte accounting, the carve gaps it produces, the extraction that
reads those extents back — works in whole-disk coordinates. So the pass adds the
partition's start as each entry passes through it. Doing it anywhere else would
mean either teaching four filesystem parsers about partitions or having the
extractor guess which window an offset belonged to.

**A path is qualified by its partition, and only when there is one.** Two
volumes on one disk very often both hold `Users/`, and both are about to be
written into one destination; without a prefix the second would collide with the
first for every file. But a source that is a single volume must keep the paths it
has always had, so the prefix exists only on the partitioned path — an image of
one volume is walked exactly as before.

**A partition that will not mount does not stop the disk.** A swap partition, an
EFI system partition this build cannot read, a volume whose superblock is gone:
all normal on a real disk, and none of them a reason to abandon the volumes that
*did* mount. Each partition's mount failure is skipped and the walk carries on,
which is the same judgement `HybridRecovery` already makes for a device with no
readable filesystem at all. A read *fault* is different, and still fatal, because
a disk that will not read is not a disk with no files.

**The carve pass is unchanged, and that is the point.** It already scans whatever
the accounting did not claim, over the whole device. Because the translated
extents are in device coordinates, the gaps it gets are the disk's real
unallocated space — including everything between partitions, which is where a
deleted partition's contents live.

**Non-conformance is reported if any volume shows it.** The flag says "something
on this disk is not what a conforming formatter writes", and an operator wants
that raised by one bad volume out of four rather than averaged away.

## Acceptance criteria

- [x] A device with no partition table is walked exactly as before: same
      entries, same paths, no prefix.
- [x] A partitioned device reports entries from every partition that mounts.
- [x] Each entry's extents are device offsets — the partition's start plus what
      the volume said.
- [x] Each entry's path is prefixed with its partition, and the prefix is
      absent for an unpartitioned source.
- [x] A partition that will not mount is skipped and the others still report.
- [x] `recordsScanned` and `entriesReported` are the sums across partitions;
      `nonConformingVolume` is true if any volume set it.
- [x] A device whose table lists no partitions is walked whole rather than not
      at all.

## Test plan

Unit (`tests/unit/recovery/PartitionedWalkTest.cpp`): an unpartitioned volume
walks unchanged; a two-partition disk reports both volumes' entries; extents come
back at their whole-disk offsets; paths carry their partition; a disk whose
second partition holds nothing mountable still reports the first; the summed
stats.

Integration (`tests/integration/WholeDiskRecoveryTest.cpp`): the four-filesystem
disk `imagegen::disk::buildMbrDiskImage` builds, recovered end to end in one run
— files from more than one volume land in the destination, under their own
partitions, with their bytes intact.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
