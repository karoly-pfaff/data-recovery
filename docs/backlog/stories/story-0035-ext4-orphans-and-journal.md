<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0035: ext4 orphans, the journal hint, and mounting

- Epic: [epic-m3-filesystem-breadth](../epic-m3-filesystem-breadth.md)
- Status: Done
- Size: L

## Goal

Turn the parsers [story-0034](story-0034-ext4-superblock-and-inodes.md) built
into a mounted filesystem: locate a volume's inodes, map a file's blocks through
its extent tree, walk the directory tree, find the deleted entries ext4 hides
inside its own records, follow the orphan list, and recover from the journal the
extent tree a deletion wiped. Hands the result to `fs::mountVolume` as its
fourth and last mounter, closing epic M3.

## Design references

- [Filesystem layer](../../architecture/filesystems.md) — ext4's row: deleted
  files come from inodes and the orphan list, name recovery is **partial**, and
  "some deletes wipe blocks".
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — every count and
  depth a walk takes off the disk is bounded.
- [story-0033](story-0033-exfat-bitmap-and-deleted-sets.md) — the shape this
  follows: the walk shares its skeleton with the others and keeps its meaning to
  itself.

## Design decisions

**ext4 hides a deleted entry rather than marking it.** FAT stamps `0xE5` over a
name's first byte and exFAT clears one bit; ext4 does neither. It adds the
deleted entry's record length to the *previous* entry's, so the previous record
swallows it and every reader walking record to record steps straight over it.
The bytes are still there. Finding them is therefore a **search of the hole**
behind each live entry, not a read — which is exactly why ext4 name recovery is
partial: an entry whose neighbour was later shortened, or whose hole was reused,
leaves nothing to find.

**A candidate found in a hole has to earn its place.** Any four aligned bytes can
be read as an inode number. A candidate is accepted only if its inode is one the
volume could have, its record length is a plausible distance, its name fits
inside that record, and the name is made of bytes a name can be made of. Even
then it is graded `kUncertain` — it is evidence, not a record.

**A deleted name whose inode has been reused gets no extents.** The name in the
hole is real; whether the inode still describes *that* file is a different
question, and a link count above zero answers it: something else owns those
blocks now. The entry is still reported, because the name is a fact, but handing
back a live file's bytes would be worse than handing back none. This is exFAT's
allocation-bitmap rule arriving by a different road.

**The hard case is the one ext4 is known for: the extent tree is gone.** Many
kernels zero an inode's `i_block` when they free it. The name survives in the
hole and the inode survives in its table, but nothing says where the bytes were.
Such an entry is reported with its name, its size and its times, and **no
extents** — which `RecoveredEntry` documents as carve territory — rather than
with a guess. FAT32's contiguity assumption is not available here: ext4 files are
routinely fragmented and an ext4 volume is routinely large.

**...unless the journal still remembers.** ext4 journals metadata, so an inode
table block is written into the journal on its way to disk. A block freed and
zeroed today may sit in the journal beside an *older* copy of itself from a
transaction before the deletion — one whose extent tree is intact. The journal is
therefore searched for every copy of the block that holds the inode, and the
first copy whose tree parses and points somewhere is used. The entry stays
`kUncertain`: the tree is real but stale, and whether those blocks still hold the
file is what the carve pass settles.

**The journal is read, never replayed** (ADR-0005). Replay is a write, and this
build does not write to the source device. What is taken from the journal is a
*hint* — an older copy of some bytes — and it is used only to locate content the
live metadata no longer locates.

**A journal feature this build does not understand ends the hint, not the
walk.** jbd2's checksum-v3 tags are laid out differently from the classic ones,
and reading one as the other yields block numbers that address the wrong blocks
entirely. A journal carrying a feature that changes the tag layout is declined
whole, and the volume walks on without the hint.

**An unwritten extent or a hole makes the whole mapping unusable.** ext4 can
allocate blocks without writing them, and can leave gaps in a file's block
numbering. Neither can be spelled in `RecoveredEntry`, whose extents are a
concatenation, and both would silently pad a recovered file with bytes that were
never in it. NTFS's sparse runs already have this rule; ext4 follows it.

**An orphan has no name to recover, so it is reported by number.** An inode on
the orphan list was unlinked while still open: no directory entry points at it
anywhere, and none ever will again. Its content is what is recoverable, so it is
reported as `#<inode>` and graded `kOrphaned` — where such a file is *written* is
the sink's policy, not the parser's.

**ext4 is probed last.** Its magic is sixteen bits a kilobyte into the volume,
which is the weakest signature of the four; asking the three that name
themselves in sector 0 first costs nothing and removes the question.

## Scope

1. **Block and inode access** (`src/fs/ext4/BlockReader`, `InodeTable`) — reading
   a block, and finding the inode a number names through its group's descriptor.
2. **Extent walk** (`src/fs/ext4/ExtentWalk`) — an inode's tree followed to its
   leaves, interior nodes read from the volume, bounded in depth and in nodes,
   and restated as device extents trimmed to the file's size.
3. **Directory walk** (`src/fs/ext4/DirectoryWalk`) — the tree from inode 2 down,
   on the shared worklist, reporting each file it names.
4. **Hole search** (`src/fs/ext4/DirectoryHole`) — the deleted entries lying
   behind the live ones.
5. **Orphan list** (`src/fs/ext4/OrphanList`) — `s_last_orphan` followed through
   each orphan's `i_dtime`, cycle- and length-bounded.
6. **Journal hint** (`src/fs/ext4/Journal`) — the jbd2 superblock, its descriptor
   blocks, and an older copy of an inode whose tree was wiped.
7. **Mounting** (`src/fs/ext4/Ext4FileSystem`) — the fourth entry in
   `fs::mountVolume`'s table.
8. **A synthetic ext4 image** under `tools/imagegen/ext4/`, plus the integration
   test that mounts it through the real front door.

## Acceptance criteria

- [x] `fs::mountVolume` mounts an ext4 volume and walks its tree, after NTFS,
      exFAT and FAT32 have each declined it.
- [x] A live file comes back with its name, its size, its times and extents that
      hold its bytes; a fragmented file's extents are read from its tree, so its
      content reads back exactly.
- [x] A file under a subdirectory keeps its place in the tree, and `.` and `..`
      never become entries or send the walk climbing.
- [x] A deleted file is found in the hole its neighbour's record swallowed, and
      comes back named, graded `kUncertain`.
- [x] A deleted file whose inode still has its extent tree comes back with its
      bytes.
- [x] A deleted file whose inode was wiped comes back with **no extents** unless
      the journal holds an older copy of its inode — and with the journal's
      extents when it does.
- [x] A deleted name whose inode has since been handed to a live file is reported
      with no extents.
- [x] An inode on the orphan list is reported as `#<inode>`, `kOrphaned`, with
      the extents its own tree still names.
- [x] The walk keeps its own worklist; no crafted tree can drive it off the C++
      stack, each directory inode is visited once, and the orphan chain cannot
      cycle.
- [x] An extent tree with an unwritten extent, a hole, or a block past the volume
      yields no extents rather than the wrong ones.
- [x] A journal whose feature flags change its tag layout is declined whole.
- [x] A synthetic ext4 image under `tools/imagegen/ext4/` holds a live fragmented
      file, a live file in a subdirectory, a deleted file with its tree intact, a
      deleted file whose tree was wiped but whose journal copy survives, and an
      orphan.
- [x] `Ext4EnumerateFuzz` and `Ext4JournalFuzz` exist with seeded corpora.

## Test plan

Unit (`tests/unit/fs/ext4/ExtentWalkTest.cpp`): an inline tree; a two-level tree
whose interior node is read from the volume; a depth that exceeds the bound; an
unwritten extent; a hole; an extent past the volume.

Unit (`tests/unit/fs/ext4/DirectoryHoleTest.cpp`): a hole holding one deleted
entry; a hole holding two; a hole of zeros; a hole holding bytes that only look
like an entry; a live entry with no hole at all.

Unit (`tests/unit/fs/ext4/OrphanListTest.cpp`): a chain of two; an empty list; a
chain that points at itself; a chain that runs past the volume's inode count.

Unit (`tests/unit/fs/ext4/JournalTest.cpp`): a journal superblock; a descriptor
block whose tags name two blocks; a journal with an unsupported feature; a
journal whose copy of an inode block still holds an extent tree.

Unit (`tests/unit/fs/ext4/MountTest.cpp`): a volume built in memory — superblock,
group descriptor, inode table, root directory — mounted through
`fs::mountVolume` and walked.

Integration (`tests/integration/Ext4EnumerationTest.cpp`): the synthetic image
mounted through the real front door; every acceptance criterion above asserted
against the fixture the image builder states.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked; epic M3 closed.
