// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant::imagegen::ext4 {

// The geometry of the synthetic ext4 volume. Every derived offset is computed
// here, so no builder and no test spells out a byte position of its own.
struct Ext4Layout {
	std::uint32_t blockSizeBytes;
	std::uint32_t totalBlocks;
	std::uint32_t totalInodes;
	std::uint32_t blocksPerGroup;
	std::uint32_t inodesPerGroup;
	std::uint32_t inodeSizeBytes;

	[[nodiscard]] std::uint64_t blockOffset(std::uint64_t block) const noexcept;
	[[nodiscard]] std::uint64_t inodeOffset(std::uint32_t number) const noexcept;
	[[nodiscard]] std::uint64_t totalBytes() const noexcept;
};

// The one fixed plan the whole fixture is built from: 1024-byte blocks, so the
// superblock is a block of its own and the volume's data starts at block 1 —
// the case ext4's `s_first_data_block` exists for.
[[nodiscard]] Ext4Layout makeExt4Layout() noexcept;

// The volume's own metadata blocks.
inline constexpr std::uint32_t kSuperblockBlock = 1;
inline constexpr std::uint32_t kGroupDescriptorBlock = 2;
inline constexpr std::uint32_t kInodeTableBlock = 5;

// Where the fixture's content lives. Spelled out so the fragmentation of
// `keep.txt` — and the fact that the journal sits far away from everything it
// remembers — are visible layout decisions rather than something a builder
// derived.
inline constexpr std::uint32_t kRootDirBlock = 30;
inline constexpr std::uint32_t kPhotosDirBlock = 31;
inline constexpr std::uint32_t kKeepFirstBlock = 40;
inline constexpr std::uint32_t kKeepSecondBlock = 60;
inline constexpr std::uint32_t kInnerBlock = 70;
inline constexpr std::uint32_t kGoneBlock = 80;
inline constexpr std::uint32_t kWipedBlock = 90;
inline constexpr std::uint32_t kOrphanBlock = 100;
inline constexpr std::uint32_t kLaterBlock = 110;
inline constexpr std::uint32_t kJournalFirstBlock = 120;
inline constexpr std::uint32_t kJournalBlocks = 32;

// The inodes the fixture uses. 2 and 8 are ext4's own — the root directory and
// the journal — and the rest start where a formatter's first user file does.
inline constexpr std::uint32_t kRootInode = 2;
inline constexpr std::uint32_t kJournalInode = 8;
inline constexpr std::uint32_t kKeepInode = 11;
inline constexpr std::uint32_t kPhotosInode = 12;
inline constexpr std::uint32_t kInnerInode = 13;
inline constexpr std::uint32_t kGoneInode = 14;
inline constexpr std::uint32_t kWipedInode = 15;
inline constexpr std::uint32_t kOrphanInode = 16;
inline constexpr std::uint32_t kLaterInode = 17;

} // namespace revenant::imagegen::ext4
