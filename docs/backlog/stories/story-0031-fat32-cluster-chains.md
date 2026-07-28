<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0031: FAT32 cluster chains, the directory walk, and mounting

- Epic: [epic-m3-filesystem-breadth](../epic-m3-filesystem-breadth.md)
- Status: Done
- Size: M

## Goal

Turn the parsers [story-0030](story-0030-fat32-directory-entries.md) built into a
mounted filesystem: follow the FAT to find where a file's bytes are, walk the
directory tree to find what it is called, and hand the result to
`fs::mountVolume` as the second entry in its probe table.

## Design references

- [story-0029](story-0029-filesystem-seam.md) — the seam this arrives behind, and
  its rule that a mounter recognizing its own signature owns the answer.
- [Filesystem layer](../../architecture/filesystems.md) — recoverability grading
  is on metadata integrity alone.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — bounded
  allocation: no on-disk chain sizes a loop unchecked.

## Scope

1. **`fat::FatTable`** — one FAT entry read at a time, and the chain that follows
   from it, bounded by the volume's own cluster count.
2. **Chain to extents** — cluster runs coalesced into `fs::Extent`s and trimmed
   to the file's declared size.
3. **The walk** — every directory from the root down, its long names assembled
   from the fragments that precede each short entry, reported as
   `fs::RecoveredEntry`.
4. **`mountFat32`** — the second entry in `fs::mountVolume`'s probe table.
5. **`tools/imagegen/fat`** — a synthetic FAT32 volume holding live, deleted and
   orphaned files, on the model of the NTFS builder.
6. **The non-conforming warning reaches the operator** — the fact story-0030's
   parser recorded travels out through `fs::EnumerationStats` and
   `recovery::RecoveryStats` to the run summary, on the path `filesystemMounted`
   already takes.

## Design decisions

**A deleted file's chain is gone, so its extents are a guess that says it is
one.** Deletion frees the file's FAT entries: the chain that said where cluster 2
of the file went is zeroed, and nothing on the volume holds it. The only thing
left is the first cluster, which the directory entry still names. So a deleted
file's content is read as the *contiguous* run its declared size needs — correct
for an unfragmented file, wrong for a fragmented one — and the entry is graded
`kUncertain` so the hybrid pass carves the region anyway. This is the single
biggest difference from NTFS, whose runlist survives deletion intact.

**Deleted directories are walked, and what is under them is orphaned.** Deleted
files mostly live inside directories that were deleted with them, so refusing to
descend would lose the majority of what a FAT undelete is for. A deleted
directory's own clusters are a guess by the rule above, so anything found under
one is reported `kOrphaned` rather than `kDeleted`: its name is real, its place
in the tree is not.

**`.` and `..` are skipped, and the walk is depth-bounded.** The dot entries are
the format's own bookkeeping, and following `..` would loop. Depth and total
entry count are both bounded by named constants, because every number the walk
trusts came off the disk (ADR-0009).

**A long name is assembled from physical order, not from ordinals.** Deletion
overwrites the ordinal byte of *every* slot in an entry set, so a deleted file's
fragments no longer say where they go. Physical order still does — fragments are
stored last-first, immediately before their short entry — so the assembler
reverses what it collected rather than sorting by a number that may be gone. A
live set's ordinals are checked against that order, and a mismatch drops the long
name back to the short one rather than assembling a name from unrelated slots.

**The warning is a note on the summary, not a refusal.** A volume below the
65525-cluster minimum is not what a conforming formatter writes, so the operator
is told — on the same discovery line that already says when no filesystem
mounted. Refusing to read it would throw away files that are plainly there.

## Acceptance criteria

- [x] `FatTable::open` reads the FAT at the geometry's offset; `chainFrom`
      returns the cluster list, stopping at an end-of-chain marker (`>= 0x0FFFFFF8`).
- [x] A free (`0`) or bad (`0x0FFFFFF7`) entry inside a chain ends it with
      `kInvalidArgument`; a cluster number outside the data region does too.
- [x] A chain longer than the volume's cluster count is `kOutOfRange` — a crafted
      cycle cannot hang the walk.
- [x] Contiguous clusters coalesce into one extent; the last extent is trimmed so
      the extents sum to the file's declared size.
- [x] A live file's extents come from its chain; a deleted file's are the
      contiguous run its size needs, and it is graded `kUncertain`.
- [x] The walk reports live files as `kLive`/`kValid`, deleted files as
      `kDeleted`/`kUncertain`, and anything under a deleted directory as
      `kOrphaned`.
- [x] Long names are assembled from the fragments preceding a short entry;
      a set whose ordinals contradict its physical order falls back to the short
      name.
- [x] `.`, `..` and the volume label report no entry.
- [x] `mountFat32` declines a non-FAT32 volume with `kNotFound` and owns the
      answer for one that names FAT32 but will not parse.
- [x] `fs::mountVolume` mounts the synthetic FAT32 image and enumerates it.
- [x] A run over a volume below the cluster minimum says so in its summary.
- [x] `Fat32EnumerateFuzz` drives mount-and-walk over arbitrary bytes.

## Test plan

Unit (`tests/unit/fs/fat/FatTableTest.cpp`): a single-cluster chain; a
three-cluster chain; a fragmented chain coalescing into two extents; a chain into
a free entry; a chain into a bad cluster; a cluster past the data region; a cycle.

The walk has **no unit test of its own, deliberately**: every case its plan
listed — a live file, a deleted file, a subdirectory, a deleted subdirectory's
children, a long name, the dot entries — needs a directory laid out on a device,
which is exactly what the image builder produces. A second hand-built fixture
would be the same volume written twice, and the one that drifted would be the
one nobody noticed. The integration test drives all of it through the real front
door instead.

Integration (`tests/integration/Fat32EnumerationTest.cpp`): the synthetic image
mounted through `fs::mountVolume`; the fragmented live file read back
byte-identical through its chain, the deleted one through the contiguity guess,
the file under the deleted directory reported as an orphan, and the dot entries
reported as nothing.

Fuzz (`tests/fuzz/Fat32EnumerateFuzz.cpp`): arbitrary bytes mounted and walked —
the BPB, every FAT entry a chain runs through, every slot, and every
subdirectory those slots point at.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
