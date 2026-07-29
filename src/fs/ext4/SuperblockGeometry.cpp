// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <limits>

#include "SuperblockInternal.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/ext4/Superblock.hpp"

namespace revenant::fs::ext4 {

namespace {

// The volume has to hold at least one block of data past the superblock's own
// before any of the rest is worth deriving.
[[nodiscard]] Result<std::uint64_t> dataBlocks(const SuperblockFields& fields) {
	if (fields.totalBlocks <= fields.firstDataBlock) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kBlockCountOffset};
	}
	return fields.totalBlocks - fields.firstDataBlock;
}

// Block groups are as many as it takes to cover the data blocks, the last one
// short if the division does not come out even. A count past 32 bits is not a
// volume, it is a block count crafted to make a walk run forever (ADR-0009).
[[nodiscard]] Result<std::uint32_t>
groupCount(const SuperblockFields& fields, std::uint64_t blocks) {
	const std::uint64_t groups = ((blocks - 1) / fields.blocksPerGroup) + 1;
	if (groups > std::numeric_limits<std::uint32_t>::max()) {
		return Error{.code = ErrorCode::kOverflow, .offset = kBlockCountOffset};
	}
	return static_cast<std::uint32_t>(groups);
}

[[nodiscard]] Ext4Geometry assemble(const SuperblockFields& fields, std::uint32_t groups) {
	return Ext4Geometry{
		.blockSizeBytes = fields.blockSizeBytes,
		.totalBlocks = fields.totalBlocks,
		.totalInodes = fields.totalInodes,
		.blocksPerGroup = fields.blocksPerGroup,
		.inodesPerGroup = fields.inodesPerGroup,
		.inodeSizeBytes = fields.inodeSizeBytes,
		.descriptorSizeBytes = fields.descriptorSizeBytes,
		.firstDataBlock = fields.firstDataBlock,
		.groupDescriptorBlock = std::uint64_t{fields.firstDataBlock} + 1,
		.groupCount = groups,
		.lastOrphanInode = fields.lastOrphanInode,
		.usesExtents = (fields.featureIncompat & kIncompatExtents) != 0};
}

} // namespace

Result<Ext4Geometry> geometryOf(const SuperblockFields& fields) {
	return dataBlocks(fields)
		.andThen([&](std::uint64_t blocks) { return groupCount(fields, blocks); })
		.map([&](std::uint32_t groups) { return assemble(fields, groups); });
}

} // namespace revenant::fs::ext4
