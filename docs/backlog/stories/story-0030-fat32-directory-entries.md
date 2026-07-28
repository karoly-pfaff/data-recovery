<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0030: FAT32 geometry and directory entries

- Epic: [epic-m3-filesystem-breadth](../epic-m3-filesystem-breadth.md)
- Status: Done
- Size: L

## Goal

Read a FAT32 volume's two on-disk vocabularies: the BPB that says where
everything is, and the 32-byte directory entry that says what a file is called,
where it starts, and whether it was deleted. Pure byte parsers with no device
and no traversal — [story-0031](story-0031-fat32-cluster-chains.md) walks the
directories these produce and follows the chains they point at.

## Design references

- [Filesystem layer](../../architecture/filesystems.md) — FAT32's row: deleted
  entries are found by the `0xE5` marker, and name recovery is *partial* because
  that marker overwrites the first character.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — bounded
  allocation: no on-disk count sizes an allocation or a loop unchecked.
- [ADR-0010](../../architecture/adr/adr-0010-filename-decoding-safe-output.md) —
  a name off a disk is untrusted bytes; it is decoded losslessly or escaped,
  never silently substituted.
- [story-0009](story-0009-ntfs-boot-sector.md) — the NTFS boot-sector parser is
  the shape this one follows: one validator per field, each reporting its own
  byte offset, folded into a validated geometry struct.

## Scope

Three independently tested units under `revenant::fs::fat`:

1. **Geometry** (`include/revenant/fs/fat/BootSector.hpp`,
   `src/fs/fat/BootSector.cpp` + `BootSectorFields.cpp`) —
   `parseFat32BootSector` validates the BPB and derives `Fat32Geometry`: cluster
   size, where the FATs are, where cluster 2 begins, how many data clusters
   there are, and which cluster the root directory starts at.
2. **Directory entries** (`include/revenant/fs/fat/DirectoryEntry.hpp`,
   `src/fs/fat/DirectoryEntry.cpp`) — one 32-byte slot classified
   (`classifyEntry`: end of directory, long-name fragment, volume label,
   directory, file) and then read by the parser its kind calls for
   (`parseShortEntry`, `parseLongNameFragment`). A short entry carries its first
   cluster, its size, its timestamps, and whether `0xE5` marked it deleted.
   Deletion is not a kind of its own, because it does not change what the slot
   is — only what one byte of it says.
3. **Names** (`src/fs/fat/ShortName.cpp`, `src/fs/fat/LongName.cpp`) — the 8.3
   short name decoded to UTF-8, and a long-name fragment's 13 UTF-16 code units
   extracted for the assembler in story-0031.

Plus `src/fs/fat/DosTime.cpp`: DOS date/time pairs converted to the layer's
FILETIME ticks.

**Three things NTFS already had are moved rather than copied**, because this is
the story where each acquires its second caller:

- `src/fs/SafeArith.{hpp,cpp}` — the overflow-checked multiplications that were
  private to the NTFS boot sector, plus the `safeAdd64` FAT32's data-region
  arithmetic needs.
- `src/fs/BpbFields.{hpp,cpp}` — NTFS's boot sector *is* a BIOS parameter block,
  so its sector size, cluster size and boot signature carry the same rules as
  FAT32's, at the same offsets. One copy, two filesystems. (exFAT is not a
  client: it states geometry as log2 exponents in different fields.)
- `src/fs/NameEscape.{hpp,cpp}` — the `%XX` / `%uXXXX` escape ADR-0010 mandates,
  which both name decoders now emit.

## Design decisions

**`FAT32   ` at offset 0x52 is the recognition token, and the parser does not
know that.** The specification calls `BS_FilSysType` informational and says the
real test is deriving the cluster count. That derivation is exactly what this
parser does anyway — but it cannot be the *probe*, because on a volume whose BPB
is damaged it would answer "not FAT32" for something that plainly is one, and
the mount table would move on (story-0029: a mounter that recognizes its own
signature owns the answer). So the string is what story-0031's mounter checks
first, and it is checked here too, as the parser's own first field — rejected
with `kInvalidArgument` at `0x52` like any other bad field. Translating that
into the mount table's `kNotFound` is the mounter's job, exactly as it is for
NTFS's OEM id. The parser stays a parser.

**The 65525-cluster minimum is not enforced.** It is the rule that tells a
*formatter* to write FAT32 rather than FAT16, and a volume below it is
malformed. It is not a rule about whether the bytes can be read: refusing to
enumerate a 40 000-cluster volume that says `FAT32` in its BPB and parses
cleanly would refuse data that is plainly there. What *is* enforced is
everything the arithmetic depends on — no zero divisors, no overflow, a data
region inside the volume, a root cluster inside the data region.

**A deleted entry's first character is gone, and the name says so.** `0xE5`
overwrites it; nothing on the volume holds the original. The decoded name
therefore begins with `_` — a documented placeholder, not a guess — and comes
back with `lossless == false`, which is what story-0031 grades `kUncertain` on.
The one exception the format defines is handled: `0x05` in the
first byte means the character really is `0xE5` (a live Japanese-locale name),
which is restored rather than treated as a deletion.

**Long-name fragments are parsed but not assembled here.** A long name is spread
across the entries *preceding* its short entry, so assembling one needs the
directory walk that story-0031 builds. This story stops at "these 13 code units,
this ordinal, this checksum" — one slot in, one fragment out, which is what
keeps the parser a parser.

**Short names are decoded as ASCII with escapes, not as a code page.** The 8.3
name is in the volume's OEM code page, which the volume does not record. Guessing
CP437 would silently mistranslate names from any other locale. Bytes `0x20`–`0x7E`
decode as themselves; anything else is escaped `%XX`, the same lossless
convention ADR-0010 already applies to undecodable UTF-16 — and the same
`lossless` flag comes back, so grading can see that the name is approximate.

## Acceptance criteria

### `parseFat32BootSector`

- [x] Returns `Fat32Geometry{bytesPerSector, bytesPerCluster, fatCount,
      fatOffsetBytes, fatSizeBytes, dataOffsetBytes, totalClusters,
      rootCluster}`.
- [x] Input shorter than 512 bytes is `kOutOfRange`.
- [x] A `BS_FilSysType` other than `FAT32   ` is `kInvalidArgument` at offset
      `0x52`; `fat::filSysTypeIsFat32` exposes the same check to story-0031's
      mounter, which is what turns it into the mount table's `kNotFound`.
- [x] Bytes-per-sector outside {512, 1024, 2048, 4096} is `kInvalidArgument` at
      offset `0x0B`; sectors-per-cluster not a power of two in 1..128 is
      `kInvalidArgument` at `0x0D`.
- [x] A zero reserved-sector count (`0x0E`) or zero FAT count (`0x10`) is
      `kInvalidArgument`.
- [x] A non-zero root-entry count (`0x11`), a non-zero 16-bit total-sector count
      (`0x13`), or a non-zero 16-bit FAT size (`0x16`) is `kInvalidArgument` —
      each of the three says this is FAT12/16, not FAT32.
- [x] A zero 32-bit FAT size (`0x24`) or zero total sectors (`0x20`) is
      `kInvalidArgument`.
- [x] A data region starting at or past the end of the volume is
      `kInvalidArgument`; a volume with zero data clusters is `kInvalidArgument`.
- [x] A root cluster outside `[2, totalClusters + 1]` is `kInvalidArgument`.
- [x] A missing `0x55AA` boot signature is `kInvalidArgument` at `0x1FE`.
- [x] Every derived byte offset is overflow-checked (`kOverflow`).

### `classifyEntry` / `parseShortEntry` / `parseLongNameFragment`

- [x] A span shorter than 32 bytes is `kOutOfRange`.
- [x] A first byte of `0x00` is `kEndOfDirectory`; the walk stops there.
- [x] An attribute byte of exactly `0x0F` is `kLongName`, whatever the first
      byte — including `0xE5`, since a deleted file's fragments still hold its
      characters.
- [x] A first byte of `0xE5` on a non-long-name entry is a deleted short entry.
- [x] The volume-label bit (`0x08`) yields `kVolumeLabel`; the directory bit
      (`0x10`) yields `kDirectory`; anything else is `kFile`.
- [x] The first cluster is `(FstClusHI << 16) | FstClusLO`, and a long-name
      fragment's `FstClusLO` (which must be zero) is not read as one.
- [x] `sizeInBytes` comes from `0x1C` as an unsigned 32-bit value.
- [x] Creation and write timestamps convert from DOS date/time to FILETIME
      ticks; an out-of-range field (month 0 or 13, day 0 or 32, hour 24,
      minute 60, second field 30) yields a zero timestamp rather than a
      fabricated date.

### Names

- [x] `keep.jpg` round-trips from its `KEEP    JPG` short entry, lower-cased per
      the `NTRes` case flags.
- [x] A deleted entry's name begins with `_` and reports `lossless == false`.
- [x] A first byte of `0x05` decodes as `0xE5`, escaped `%E5`, and is *not*
      treated as deleted.
- [x] A trailing-space-padded 8.3 name loses its padding, and a name with no
      extension gets no trailing dot.
- [x] A long-name fragment yields its 13 UTF-16 code units, its ordinal with the
      `0x40` last-fragment flag split out, and its checksum byte.

### Fuzz

- [x] `Fat32BootSectorFuzz` drives `parseFat32BootSector` with arbitrary bytes.
- [x] `FatDirectoryEntryFuzz` drives `parseDirectoryEntry` and the name decoders
      with arbitrary bytes; every decoded name is valid UTF-8.
- [x] Both have `tests/fuzz/corpus/<Target>/` with a seed, so the CI fuzz job
      starts.

## Test plan

Unit (`tests/unit/fs/fat/BootSectorTest.cpp`): a known-good BPB parses to known
geometry; one case per rejection above, asserting both the code and the reported
offset; the `FAT32   ` mismatch asserts `kNotFound` specifically.

Unit (`tests/unit/fs/fat/DirectoryEntryTest.cpp`): one case per entry kind; a
deleted file; a `0x05` first byte; a long-name fragment with and without the
last flag; a truncated span; cluster numbers spanning the 16-bit boundary.

Unit (`tests/unit/fs/fat/NameTest.cpp`): the 8.3 cases above, plus a name with a
high-bit byte escaping to `%XX`.

Unit (`tests/unit/fs/fat/DosTimeTest.cpp`): the FAT epoch (1980-01-01 00:00:00)
against its known FILETIME; a known mid-range stamp; each out-of-range field.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
