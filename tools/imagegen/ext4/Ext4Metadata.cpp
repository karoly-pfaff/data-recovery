// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ext4/Ext4Metadata.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ext4/Ext4Layout.hpp"
#include "imagegen/ext4/Ext4Records.hpp"

namespace revenant::imagegen::ext4 {

namespace {

constexpr std::uint32_t kIncompatExtents = 0x40;

void putVolumeCounts(std::vector<std::byte>& image, const Ext4Layout& layout, std::size_t at) {
	putLe<std::uint32_t>(image, at + 0x00, layout.totalInodes);
	putLe<std::uint32_t>(image, at + 0x04, layout.totalBlocks);
	putLe<std::uint32_t>(image, at + 0x20, layout.blocksPerGroup);
	putLe<std::uint32_t>(image, at + 0x28, layout.inodesPerGroup);
}

// 1024-byte blocks, so the superblock is a block of its own and the volume's
// data starts at block 1 — which is what `s_first_data_block` records.
void putVolumeShape(std::vector<std::byte>& image, const Ext4Layout& layout, std::size_t at) {
	putLe<std::uint32_t>(image, at + 0x14, kSuperblockBlock);
	putLe<std::uint32_t>(image, at + 0x18, 0);
	putLe<std::uint16_t>(image, at + 0x38, 0xEF53);
	putLe<std::uint16_t>(image, at + 0x58, static_cast<std::uint16_t>(layout.inodeSizeBytes));
	putLe<std::uint32_t>(image, at + 0x60, kIncompatExtents);
	putLe<std::uint32_t>(image, at + 0x64, kOrphanInode);
}

} // namespace

void putExt4Metadata(std::vector<std::byte>& image, const Ext4Layout& layout) {
	const auto superblock = static_cast<std::size_t>(layout.blockOffset(kSuperblockBlock));
	putVolumeCounts(image, layout, superblock);
	putVolumeShape(image, layout, superblock);
	const auto descriptor = static_cast<std::size_t>(layout.blockOffset(kGroupDescriptorBlock));
	putLe<std::uint32_t>(image, descriptor + 0x08, kInodeTableBlock);
}

void putExt4Inode(
	std::vector<std::byte>& image,
	const Ext4Layout& layout,
	std::uint32_t number,
	const InodeSpec& spec) {
	const auto at = static_cast<std::size_t>(layout.inodeOffset(number));
	putBytes(image, at, inodeRecord(spec, layout.inodeSizeBytes));
}

std::vector<std::byte> emptyExt4Volume(const Ext4Layout& layout) {
	std::vector<std::byte> image(static_cast<std::size_t>(layout.totalBytes()), std::byte{0});
	putExt4Metadata(image, layout);
	return image;
}

} // namespace revenant::imagegen::ext4
