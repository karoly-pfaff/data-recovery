<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0034: ext4 superblock, inodes and extent trees

- Epic: [epic-m3-filesystem-breadth](../epic-m3-filesystem-breadth.md)
- Status: Done
- Size: L

## Goal

Read ext4's on-disk vocabulary: the superblock that states the volume's
geometry, the group descriptors that say where each block group keeps its inode
table, the inodes themselves, the extent trees that map a file's blocks, and the
linear directory entries that name it. Pure byte parsers, no device and no
traversal — [story-0035](story-0035-ext4-orphans-and-journal.md) walks them.

## Design references

- [Filesystem layer](../../architecture/filesystems.md) — ext4's row: deleted
  files are found through inodes and the orphan list, and name recovery is
  **partial**.
- [ADR-0010](../../architecture/adr/adr-0010-filename-decoding-safe-output.md) —
  ext4 "stores raw bytes with no enforced encoding", so it needs a decoder
  neither NTFS's UTF-16 nor FAT's code page fits.
- [story-0032](story-0032-exfat-boot-and-entry-sets.md) — the shape this
  follows: one validator per field, each reporting its own byte offset.

## Scope

1. **Superblock** (`include/revenant/fs/ext4/Superblock.hpp`) —
   `parseExt4Superblock` validates the 1024-byte superblock and derives
   `Ext4Geometry`: block size, how many blocks and inodes there are, how they
   are divided into groups, how large an inode and a group descriptor are, where
   the group descriptor table starts, and the head of the orphan list.
2. **Group descriptors** (`include/revenant/fs/ext4/GroupDescriptor.hpp`) —
   `parseGroupDescriptor` reads one 32- or 64-byte slot for the one thing a walk
   needs from it: which block holds that group's inode table.
3. **Inodes** (`include/revenant/fs/ext4/Inode.hpp`) — `parseExt4Inode` reads
   mode, size, link count, flags, the four timestamps, the deletion time, and
   the 60-byte block map, and says whether the inode is a directory, a regular
   file, and whether its block map is an extent tree.
4. **Extent trees** (`include/revenant/fs/ext4/ExtentTree.hpp`) —
   `parseExtentHeader`, `parseExtentLeaves` and `parseExtentIndices` read one
   node, whether it sits in an inode's 60 bytes or in a block of its own.
5. **Directory entries** (`include/revenant/fs/ext4/DirectoryEntry.hpp`) —
   `parseExt4DirEntry` reads one linear entry: the inode it names, how far the
   next entry is, what kind of thing it is, and its raw name bytes.
6. **Raw-byte names** (`fs::decodeRawName`) and **Unix seconds**
   (`fs::filetimeFromUnixSeconds`) — the two conversions ext4 needs that the
   layer did not already have.

## Design decisions

**The superblock is 1024 bytes into the volume, not at its start.** Every other
filesystem here names itself in sector 0; ext4 leaves the first 1024 bytes for a
boot loader. The shared mount-region read therefore grew an offset — and became
`readMountRegion`, since "volume start" stopped being true of it — rather than
ext4 growing a read of its own. The three existing callers pass 0.

**Recognition is the magic *and* a block-size shift ext4 can express.** `0xEF53`
is sixteen bits: on its own it is a coincidence a RAW volume can produce, and a
mounter that claims a volume owns its answer, so a coincidence would cost the
run the carve pass it should have had. The shift at `0x18` is checked with it,
exactly as exFAT checks its must-be-zero field alongside its name. A volume that
passes both and then fails a later field is a *broken ext4*, and is reported as
one.

**Every geometry field is checked against the block size, not against a
constant.** Blocks per group and inodes per group are capped by what one block's
worth of bitmap can address; an inode and a group descriptor must both fit in a
block. These are the volume's own consistency rules, and a crafted superblock
that breaks them would otherwise be believed.

**A directory's size is not a size.** ext4 reuses `i_size_high` as `i_dir_acl`
for anything that is not a regular file, so the high half is folded in only for
regular files. Reading it unconditionally would give a directory a size in the
terabytes off two bytes of unrelated ACL data.

**`i_ctime` is not a creation time, so it is not reported as one.** ext4's
`i_ctime` is when the *inode* last changed. The real creation time is `i_crtime`
in the inode's extra area, which only exists when the inode is large enough and
says so through `i_extra_isize`. When it is not there, `created` stays zero —
the layer's "no timestamp", which is honest where a substituted `i_ctime` would
not be.

**`ee_len` above 32768 is a length *and* a flag.** ext4 marks an extent
uninitialized — allocated but never written — by adding 32768 to its length.
Both halves are reported: the real block count, and the fact that the blocks
hold nothing yet. A walk that missed the flag would hand back a file padded with
whatever those blocks last held.

**An extent tree's depth and entry count are bounded by the node that holds
them** (ADR-0009). `eh_entries` is believed only as far as the node has room
for, and a depth above 5 is rejected outright — ext4 cannot build one, and a
crafted volume that claims one is trying to make the walk chase blocks.

**ext4 names are raw bytes, so decoding them is a *validation*, not a
transcoding.** `decodeRawName` passes well-formed UTF-8 through unchanged — that
is what a Linux volume almost always holds — and escapes anything else byte by
byte as `%XX`: an invalid or overlong sequence, a surrogate, a NUL, a control
byte, and the two characters ADR-0010 never lets through as themselves, `/` and
`%`. Nothing is dropped and nothing is substituted, so the escape is reversible
and `lossless` says whether it fired.

## Acceptance criteria

- [x] `parseExt4Superblock` returns `Ext4Geometry{blockSizeBytes, totalBlocks,
      totalInodes, blocksPerGroup, inodesPerGroup, inodeSizeBytes,
      descriptorSizeBytes, firstDataBlock, groupDescriptorBlock, groupCount,
      lastOrphanInode, usesExtents}`.
- [x] Input shorter than 1024 bytes is `kOutOfRange`.
- [x] A magic other than `0xEF53` is `kInvalidArgument` at `0x38`.
- [x] A block-size shift above 6 is `kInvalidArgument` at `0x18`; a first data
      block that is not what the block size requires is `kInvalidArgument` at
      `0x14`.
- [x] A zero or over-large blocks-per-group is `kInvalidArgument` at `0x20`, and
      the same for inodes-per-group at `0x28`; an inode size that is not a power
      of two in `[128, blockSize]` is `kInvalidArgument` at `0x58`.
- [x] A 64-bit volume's descriptor size that is not a power of two in
      `[64, blockSize]` is `kInvalidArgument` at `0xFE`; a volume without the
      64-bit feature has 32-byte descriptors whatever `s_desc_size` says.
- [x] A zero inode count is `kInvalidArgument` at `0x00`, and a zero block count
      `kInvalidArgument` at `0x04`; a 64-bit volume's block count includes its
      high half at `0x150`.
- [x] Every derived value is overflow-checked, and the group count is the
      rounded-up division the volume's own numbers give.
- [x] `parseGroupDescriptor` reads the inode table block from a 32-byte
      descriptor, and its high half as well from a 64-byte one.
- [x] `parseExt4Inode` reads mode, size, link count, flags, timestamps, deletion
      time and the 60-byte block map; a non-zero `i_dtime` is what marks the
      inode deleted, and `i_size_high` counts only for a regular file.
- [x] `created` comes from `i_crtime` when the inode is large enough to hold it
      and stays zero when it is not.
- [x] `parseExtentHeader` rejects a magic other than `0xF30A` at `0x00` and a
      depth above 5 at `0x06`; `parseExtentLeaves` and `parseExtentIndices` each
      reject the other kind of node and refuse an entry count the node has no
      room for.
- [x] An `ee_len` above 32768 yields the real block count and
      `initialized == false`.
- [x] `parseExt4DirEntry` reads inode, record length, file type and name bytes;
      a record length below 8, not a multiple of 4, or past the end of the input
      is `kInvalidArgument` at `0x04`, and a name that will not fit inside the
      record is `kInvalidArgument` at `0x06`.
- [x] `decodeRawName` passes valid UTF-8 through, escapes everything else as
      `%XX`, and always produces valid UTF-8.
- [x] `Ext4SuperblockFuzz`, `Ext4InodeFuzz`, `Ext4ExtentTreeFuzz` and
      `Ext4DirectoryEntryFuzz` exist with seeded corpora, the last asserting the
      decoded name is valid UTF-8.

## Test plan

Unit (`tests/unit/fs/ext4/SuperblockTest.cpp`): a known-good superblock parses
to known geometry; one case per rejection above, asserting code *and* offset; a
64-bit volume's descriptor size and block count read from their high halves.

Unit (`tests/unit/fs/ext4/InodeTest.cpp`): a live regular file, a directory, a
deleted inode with a non-zero `i_dtime`, a large file whose size needs both
halves, a directory whose `i_size_high` must be ignored, an inode with and
without room for `i_crtime`, and a truncated slot.

Unit (`tests/unit/fs/ext4/ExtentTreeTest.cpp`): a leaf node with two extents; an
uninitialized extent; an interior node with one index; a bad magic; a depth of
6; an entry count larger than the node.

Unit (`tests/unit/fs/ext4/DirectoryEntryTest.cpp`): a file entry, a directory
entry, `.` and `..`, a record length that is not a multiple of 4, a name that
runs past its record, and a truncated slot.

Unit (`tests/unit/fs/RawNameTest.cpp`): ASCII, multi-byte UTF-8, an invalid
sequence, an overlong encoding, a surrogate, a NUL, a control byte, `/` and `%`.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
