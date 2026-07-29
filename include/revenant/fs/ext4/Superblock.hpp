// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

// ext4 leaves the volume's first kilobyte to a boot loader and puts its
// superblock immediately after it. Every other filesystem this build reads
// names itself in sector 0; ext4 is the one that does not.
inline constexpr std::uint64_t kSuperblockOffset = 1024;
inline constexpr std::size_t kSuperblockBytes = 1024;

// Inode numbering starts at 1 and the low numbers are the filesystem's own
// metadata; the root directory is always inode 2 and the journal always 8.
inline constexpr std::uint32_t kRootInode = 2;
inline constexpr std::uint32_t kJournalInode = 8;
inline constexpr std::uint32_t kFirstUserInode = 11;

// Validated ext4 geometry. Every on-disk field behind these has been checked
// against the volume's own consistency rules and every derivation
// overflow-tested, so the values are safe to use directly as block numbers,
// sizes and counts.
struct Ext4Geometry {
	std::uint32_t blockSizeBytes;
	std::uint64_t totalBlocks;
	std::uint32_t totalInodes;
	std::uint32_t blocksPerGroup;
	std::uint32_t inodesPerGroup;
	std::uint32_t inodeSizeBytes;
	// 32 bytes, or whatever the volume states when it carries the 64-bit
	// feature. A volume without that feature has 32-byte descriptors whatever
	// `s_desc_size` happens to hold.
	std::uint32_t descriptorSizeBytes;
	// 1 on a 1024-byte-block volume, where the superblock is a block of its own,
	// and 0 on every other, where it sits inside block 0.
	std::uint32_t firstDataBlock;
	// The group descriptor table begins in the block after the superblock's.
	std::uint64_t groupDescriptorBlock;
	std::uint32_t groupCount;
	// The head of the volume's orphan list: an inode that was unlinked while
	// still open, which the volume meant to free and may not have. Zero when the
	// list is empty. The chain from here runs through each orphan's `i_dtime`.
	std::uint32_t lastOrphanInode;
	// Whether inodes map their blocks with extent trees rather than the indirect
	// block lists ext2 used. Every ext4 volume sets this; a volume that does not
	// is an ext2/ext3 one wearing ext4's magic.
	bool usesExtents;
};

// Parses and validates the superblock of an ext4 volume.
//
// Truncated input yields kOutOfRange; any on-disk rule violation yields
// kInvalidArgument carrying the offending field's byte offset; arithmetic that
// would wrap yields kOverflow.
//
// The checks are the volume's own consistency rules rather than constants
// picked here: blocks and inodes per group are capped by what one block's worth
// of bitmap can address, and an inode and a group descriptor must each fit
// inside a block. A crafted superblock that breaks them would otherwise be
// believed all the way into a read.
[[nodiscard]] Result<Ext4Geometry> parseExt4Superblock(std::span<const std::byte> superblock);

} // namespace revenant::fs::ext4
