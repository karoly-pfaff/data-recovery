<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M3 — Filesystem breadth

**Goal:** add FAT32, exFAT, and ext4 recovery behind a shared `fs::FileSystem` seam,
using the same synthetic-image test methodology proven with NTFS in M1.

**Milestone:** [M3](../roadmap.md#m3--filesystem-breadth)

## The seam does not exist yet

M1 deliberately shipped without one. `fs::RecoveredEntry` and `fs::EntryVisitor` are the
shared vocabulary — every filesystem reports that shape — but there is no interface
behind them: `recovery::enumerateVolume` wires NTFS in as a free function, and
`HybridRecovery` calls it directly. That was the right call with one filesystem (one
implementation does not justify an abstraction), and it is why this epic's first story is
the seam itself rather than a parser.

Building it once, before three filesystems are written against it, is what keeps this
epic from being three independent rewrites of the same wiring.

## Outcome / definition of ready-to-close

- FAT32, exFAT, and ext4 each mount a synthetic image and enumerate live, deleted, and
  orphaned entries with the correct recoverability grading.
- Name recovery works where the filesystem preserves it (full for exFAT, partial for
  FAT32/ext4 per [filesystems.md](../architecture/filesystems.md)).
- Hybrid mode works across all four filesystems.

## Candidate stories (expanded when picked up)

| Story | Title | Size |
|-------|-------|:----:|
| story-0029 → | see [story-0029](stories/story-0029-filesystem-seam.md): `fs::FileSystem` seam + volume mounting | M |
| story-0030 → | see [story-0030](stories/story-0030-fat32-directory-entries.md): FAT32 geometry and directory entries (`0xE5` deletions) | L |
| story-0031 → | see [story-0031](stories/story-0031-fat32-cluster-chains.md): FAT32 cluster chains, the directory walk, and mounting | M |
| story-0032 → | see [story-0032](stories/story-0032-exfat-boot-and-entry-sets.md): exFAT boot region and directory entry sets | L |
| story-0033 → | see [story-0033](stories/story-0033-exfat-bitmap-and-deleted-sets.md): exFAT entry sets, the walk, and mounting | M |
| story-0034 → | see [story-0034](stories/story-0034-ext4-superblock-and-inodes.md): ext4 superblock, inodes and extent trees | L |
| story-0035 | ext4 orphan list + journal-hint recovery | L |

## Notes

- The seam is designed against NTFS and FAT32 — one filesystem it already fits and one it
  has to earn. If exFAT or ext4 will not fit behind it, that is an ADR-worthy finding, not
  a quiet widening of the interface.
- Each filesystem needs its own synthetic image builder under `tools/imagegen/`, holding
  live, deleted and orphaned entries, on the model of the NTFS one from
  [story-0065](stories/story-0065-ntfs-image-builder.md).
