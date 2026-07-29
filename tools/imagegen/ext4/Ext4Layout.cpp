// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ext4/Ext4Layout.hpp"

#include <cstdint>

namespace revenant::imagegen::ext4 {

namespace {

constexpr std::uint32_t kBlockSize = 1024;
constexpr std::uint32_t kTotalBlocks = 512;
constexpr std::uint32_t kTotalInodes = 64;
constexpr std::uint32_t kBlocksPerGroup = 8192;
constexpr std::uint32_t kInodeSize = 256;

} // namespace

std::uint64_t Ext4Layout::blockOffset(std::uint64_t block) const noexcept {
	return block * blockSizeBytes;
}

std::uint64_t Ext4Layout::inodeOffset(std::uint32_t number) const noexcept {
	return blockOffset(kInodeTableBlock) + (std::uint64_t{number - 1} * inodeSizeBytes);
}

std::uint64_t Ext4Layout::totalBytes() const noexcept {
	return std::uint64_t{totalBlocks} * blockSizeBytes;
}

Ext4Layout makeExt4Layout() noexcept {
	return Ext4Layout{
		.blockSizeBytes = kBlockSize,
		.totalBlocks = kTotalBlocks,
		.totalInodes = kTotalInodes,
		.blocksPerGroup = kBlocksPerGroup,
		.inodesPerGroup = kTotalInodes,
		.inodeSizeBytes = kInodeSize};
}

} // namespace revenant::imagegen::ext4
