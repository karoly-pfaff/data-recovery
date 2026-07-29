// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/InodeTable.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fs/ext4/BlockReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/ext4/GroupDescriptor.hpp"
#include "revenant/fs/ext4/Inode.hpp"
#include "revenant/fs/ext4/Superblock.hpp"

namespace revenant::fs::ext4 {

namespace {

// Which group holds an inode, and how far into that group's table it sits.
struct InodePlace {
	std::uint32_t group;
	std::uint32_t indexInGroup;
};

[[nodiscard]] Result<InodePlace> placeOf(const Ext4Geometry& geometry, std::uint32_t number) {
	if (number == 0 || number > geometry.totalInodes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = number};
	}
	const std::uint32_t index = number - 1;
	return InodePlace{
		.group = index / geometry.inodesPerGroup,
		.indexInGroup = index % geometry.inodesPerGroup};
}

// The group descriptor table is an array of fixed-size records starting in one
// known block, so a descriptor is found by arithmetic rather than by a search.
[[nodiscard]] Result<std::vector<std::byte>>
readDescriptor(const Ext4Blocks& blocks, std::uint32_t group) {
	const auto& geometry = blocks.geometry();
	const auto at = blocks.blockOffset(geometry.groupDescriptorBlock) +
					(std::uint64_t{group} * geometry.descriptorSizeBytes);
	std::vector<std::byte> slot(geometry.descriptorSizeBytes, std::byte{0});
	return blocks.read(at, slot).map([&slot](std::size_t) { return slot; });
}

[[nodiscard]] Result<std::uint64_t>
tableBlockOf(const Ext4Blocks& blocks, const InodePlace& place) {
	return readDescriptor(blocks, place.group)
		.andThen([&blocks](const std::vector<std::byte>& slot) {
			return parseGroupDescriptor(slot, blocks.geometry().descriptorSizeBytes);
		})
		.andThen([&blocks](const Ext4Group& group) -> Result<std::uint64_t> {
			if (!blocks.isDataBlock(group.inodeTableBlock)) {
				return Error{.code = ErrorCode::kOutOfRange, .offset = group.inodeTableBlock};
			}
			return group.inodeTableBlock;
		});
}

[[nodiscard]] std::uint64_t
inodeOffset(const Ext4Blocks& blocks, std::uint64_t tableBlock, std::uint32_t indexInGroup) {
	return blocks.blockOffset(tableBlock) +
		   (std::uint64_t{indexInGroup} * blocks.geometry().inodeSizeBytes);
}

} // namespace

Ext4InodeTable::Ext4InodeTable(const Ext4Blocks& blocks) noexcept : blocks_(&blocks) {}

Result<std::uint64_t> Ext4InodeTable::offsetOf(std::uint32_t number) const {
	return placeOf(blocks_->geometry(), number).andThen([this](const InodePlace& place) {
		return tableBlockOf(*blocks_, place).map([this, &place](std::uint64_t tableBlock) {
			return inodeOffset(*blocks_, tableBlock, place.indexInGroup);
		});
	});
}

Result<Ext4Inode> Ext4InodeTable::read(std::uint32_t number) const {
	const auto inodeBytes = blocks_->geometry().inodeSizeBytes;
	return offsetOf(number).andThen([this, inodeBytes](std::uint64_t at) {
		std::vector<std::byte> slot(inodeBytes, std::byte{0});
		return blocks_->read(at, slot).andThen(
			[&slot](std::size_t) { return parseExt4Inode(slot); });
	});
}

} // namespace revenant::fs::ext4
