<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0304: exFAT boot region and directory entry sets

- Epic: [epic-m3-filesystem-breadth](../epic-m3-filesystem-breadth.md)
- Status: Done
- Size: L

## Goal

Read exFAT's two on-disk vocabularies: the boot region that states the volume's
geometry as log2 exponents, and the 32-byte directory entries that come in
*sets* rather than singly. Pure byte parsers, no device and no traversal —
[story-0305](story-0305-exfat-bitmap-and-deleted-sets.md) walks them and reads
the allocation bitmap.

## Design references

- [Filesystem layer](../../architecture/filesystems.md) — exFAT's row: deleted
  sets are found by the cleared in-use bit, and name recovery is **full**.
- [ADR-0010](../../architecture/adr/adr-0010-filename-decoding-safe-output.md) —
  exFAT names are UTF-16, so `fs::decodeUtf16Name` decodes them as NTFS's are.
- [story-0302](story-0302-fat32-directory-entries.md) — the shape this follows:
  one validator per field, each reporting its own byte offset.

## Scope

1. **Boot region** (`include/revenant/fs/exfat/BootRegion.hpp`) —
   `parseExfatBootSector` validates the 512-byte main boot sector and derives
   `ExfatGeometry`: cluster size, where the FAT and the cluster heap are, how
   many clusters there are, and where the root directory starts.
2. **Directory entries** (`include/revenant/fs/exfat/DirectoryEntry.hpp`) — one
   32-byte slot classified by its type byte and parsed into the three kinds a
   file's set is made of: the **file** entry (attributes, timestamps, how many
   entries follow), the **stream extension** (first cluster, length, name
   length, and whether the chain is even used), and a **file name** fragment
   (15 UTF-16 code units).

## Design decisions

**`EXFAT   ` at 0x03 *and* 53 zero bytes at 0x0B are the recognition.** exFAT
deliberately puts zeros where a FAT BPB keeps its geometry, precisely so a
driver cannot mistake one for the other. Checking only the name would let a
crafted FAT volume claim to be exFAT; checking the zeros is what the format
intends, and it is why exFAT must be probed *before* FAT32 in the mount table.

**Geometry is log2, and the exponents are range-checked before they shift.** A
sector-size shift outside 9..12, or a cluster shift that would push a cluster
past 32 MiB, is rejected before anything is shifted — an unchecked shift is
undefined behaviour, not a large number.

**`NoFatChain` is a fact the parser reports, not one it acts on.** exFAT lets a
contiguous file say so and skip the FAT entirely. That is the *normal* case for
a file that was never fragmented, and it is also what makes exFAT undelete
better than FAT32's: a deleted contiguous file's extent is stated, not guessed.
Story-0033 is what uses it.

**An entry set is not assembled here.** A file is a file entry, a stream
extension and one or more name fragments, in that order — which is a property of
the *sequence*, not of any slot. This story parses slots; the walk assembles
them.

## Acceptance criteria

- [x] `parseExfatBootSector` returns `ExfatGeometry{bytesPerSector,
      bytesPerCluster, fatOffsetBytes, fatSizeBytes, clusterHeapOffsetBytes,
      totalClusters, rootCluster, fatCount}`.
- [x] Input shorter than 512 bytes is `kOutOfRange`.
- [x] A file-system name other than `EXFAT   ` is `kInvalidArgument` at `0x03`.
- [x] A non-zero byte anywhere in the 53-byte must-be-zero field is
      `kInvalidArgument` at `0x0B` — that field is what tells exFAT from FAT.
- [x] A bytes-per-sector shift outside 9..12 is `kInvalidArgument` at `0x6C`; a
      sectors-per-cluster shift pushing the cluster past 32 MiB is
      `kInvalidArgument` at `0x6D`.
- [x] A FAT count other than 1 or 2 is `kInvalidArgument` at `0x6E`.
- [x] A zero cluster count, a cluster heap starting past the volume's end, or a
      root cluster outside `[2, totalClusters + 1]` is `kInvalidArgument`.
- [x] A missing `0xAA55` signature is `kInvalidArgument` at `0x1FE`.
- [x] Every derived byte offset is overflow-checked.
- [x] `classifyExfatEntry` names the slot: end of directory (`0x00`), a file, a
      stream extension, a file name, the allocation bitmap, the volume label, or
      an unknown type — and says whether the in-use bit (`0x80`) is set.
- [x] `parseFileEntry` reads the secondary-entry count and the timestamps;
      `parseStreamExtension` reads the first cluster, the valid data length, the
      name length and `NoFatChain`; `parseFileName` yields its 15 code units.
- [x] `Fat32BootSectorFuzz`'s counterpart `ExfatBootRegionFuzz` and
      `ExfatDirectoryEntryFuzz` exist with seeded corpora.

## Test plan

Unit (`tests/unit/fs/exfat/BootRegionTest.cpp`): a known-good boot sector parses
to known geometry; one case per rejection above, asserting code *and* offset;
the must-be-zero field asserted with a non-zero byte at each end of it.

Unit (`tests/unit/fs/exfat/DirectoryEntryTest.cpp`): one case per entry kind; a
deleted (in-use bit cleared) file entry; a stream extension with and without
`NoFatChain`; a name fragment; a truncated slot.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
