<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M3 — Filesystem breadth

**Goal:** add FAT32, exFAT, and ext4 parsers behind the existing `FileSystem` interface,
using the same synthetic-image test methodology proven with NTFS in M1.

**Milestone:** [M3](../roadmap.md#m3--filesystem-breadth)

## Outcome / definition of ready-to-close

- FAT32, exFAT, and ext4 each mount a synthetic image and enumerate live, deleted, and
  orphaned entries with the correct recoverability grading.
- Name recovery works where the filesystem preserves it (full for exFAT, partial for
  FAT32/ext4 per [filesystems.md](../architecture/filesystems.md)).
- Hybrid mode works across all four filesystems.

## Candidate stories (expanded when picked up)

| Story | Title | Size |
|-------|-------|:----:|
| story-0030 | FAT32 BPB + directory-entry parser (`0xE5` deletions) | L |
| story-0031 | FAT32 cluster-chain reconstruction | M |
| story-0032 | exFAT boot region + directory entry sets | L |
| story-0033 | exFAT allocation bitmap + deleted-set recovery | M |
| story-0034 | ext4 superblock + inode/extent parser | L |
| story-0035 | ext4 orphan list + journal-hint recovery | M |

## Notes

- No new interfaces expected — this epic validates that `FileSystem` was the right seam.
  If it is not, that is an ADR-worthy finding.
