<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0033: exFAT entry sets, the walk, and mounting

- Epic: [epic-m3-filesystem-breadth](../epic-m3-filesystem-breadth.md)
- Status: Done
- Size: M

## Goal

Turn the parsers [story-0032](story-0032-exfat-boot-and-entry-sets.md) built into
a mounted filesystem: assemble the entry sets a file is spread across, walk the
directory tree, and hand the result to `fs::mountVolume` as its third mounter.

## Design decisions

**A deleted exFAT file states where its bytes are; a deleted FAT32 file does
not.** exFAT clears one bit per entry and leaves the set standing, so the stream
extension — including its `NoFatChain` flag and its length — survives. A
contiguous file therefore comes back with a *stated* extent, not FAT32's
contiguity guess. It is still graded `kUncertain`, because whether its clusters
have since been handed out again is the allocation bitmap's question and the
carve pass covers it either way.

**A set ends where the next one begins.** exFAT says how many entries follow,
but a damaged set may not deliver them, so the count is not trusted to arrive:
what was collected when the next file entry appears is what the set had.

**The walk shares its skeleton with FAT32's, not its meaning.** Reading a
directory's clusters, driving a worklist of directories, folding over
fixed-size slots and skipping a directory that will not read are the same in
both — so they live in `fs/DirectoryTreeWalk.hpp`. What a slot *means* is where
the two part company, and that stayed in each filesystem. The duplication gate
is what found each of these, one at a time.

**exFAT is probed before FAT32.** An exFAT volume also carries a FAT-shaped boot
sector; the mount table's order comment promised this from story-0029 and now
has to hold.

**The bitmap is what tells a recoverable deletion from a lost one.** exFAT keeps
a bit per cluster saying whether the volume considers it in use. A deleted set
whose first cluster is *still* marked in use has had its bytes handed to
something else: the name is real, so the entry is reported, but with no extents
— handing back a live file's bytes would be worse than handing back none, and
the region is what the carve pass is for. A volume with no readable bitmap
concludes nothing from it either way.

## Acceptance criteria

- [x] `fs::mountVolume` mounts an exFAT volume and walks its tree.
- [x] A live file comes back with its name, its size and its extent.
- [x] A deleted file keeps its whole name — exFAT takes no part of it.
- [x] A deleted contiguous file's extent is stated rather than guessed.
- [x] Directories are descended into and never reported as entries.
- [x] The walk keeps its own worklist; no crafted tree can drive it off the
      C++ stack, and each directory cluster is visited once.
- [x] The allocation bitmap is read from the entry the root directory names, and
      a deleted set whose clusters it says are in use again gets no extents.
- [x] A synthetic exFAT image under `tools/imagegen/exfat/` holds a live
      fragmented file, a live file in a subdirectory, a deleted contiguous file,
      and a deleted file whose cluster the volume handed out again.

## Test plan

Unit (`tests/unit/fs/exfat/MountTest.cpp`): a volume built in memory — boot
sector, FAT, root directory with one live and one deleted entry set — mounted
through `fs::mountVolume` and walked.

Integration (`tests/integration/ExfatEnumerationTest.cpp`): the synthetic image
mounted through the real front door; the fragmented live file read back
byte-identical through the table, the deleted one through the extent its set
stated, the file under `photos` at its place in the tree, and the deleted file
whose cluster was reused reported with no extents at all.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
