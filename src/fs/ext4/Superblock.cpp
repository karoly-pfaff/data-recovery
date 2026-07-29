// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ext4/Superblock.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

#include "SuperblockInternal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

namespace {

// Each step folds one or two validated fields into the block under
// construction, so a rejection anywhere short-circuits the whole read. The
// order is what the fields depend on: the block size bounds most of the rest,
// and the feature flags decide how wide two of them are.

[[nodiscard]] Result<SuperblockFields>
withBlockSize(const ByteReader& reader, SuperblockFields fields) {
	return blockSize(reader).map([&](std::uint32_t bytes) {
		fields.blockSizeBytes = bytes;
		return fields;
	});
}

[[nodiscard]] Result<SuperblockFields>
withFeatures(const ByteReader& reader, SuperblockFields fields) {
	return reader.readLe<std::uint32_t>(kFeatureIncompatOffset)
		.andThen([&](std::uint32_t features) {
			return reader.readLe<std::uint32_t>(kLastOrphanOffset).map([&](std::uint32_t orphan) {
				fields.featureIncompat = features;
				fields.lastOrphanInode = orphan;
				return fields;
			});
		});
}

[[nodiscard]] Result<SuperblockFields>
withLayout(const ByteReader& reader, SuperblockFields fields) {
	return firstDataBlock(reader, fields.blockSizeBytes).andThen([&](std::uint32_t first) {
		return perGroupCount(reader, kBlocksPerGroupOffset, fields.blockSizeBytes)
			.map([&](std::uint32_t blocks) {
				fields.firstDataBlock = first;
				fields.blocksPerGroup = blocks;
				return fields;
			});
	});
}

[[nodiscard]] Result<SuperblockFields>
withInodesPerGroup(const ByteReader& reader, SuperblockFields fields) {
	return perGroupCount(reader, kInodesPerGroupOffset, fields.blockSizeBytes)
		.map([&](std::uint32_t inodes) {
			fields.inodesPerGroup = inodes;
			return fields;
		});
}

[[nodiscard]] Result<SuperblockFields>
withRecordSizes(const ByteReader& reader, SuperblockFields fields) {
	return inodeSize(reader, fields.blockSizeBytes).andThen([&](std::uint32_t inodeBytes) {
		fields.inodeSizeBytes = inodeBytes;
		return descriptorSize(reader, fields).map([&](std::uint32_t descriptorBytes) {
			fields.descriptorSizeBytes = descriptorBytes;
			return fields;
		});
	});
}

[[nodiscard]] Result<SuperblockFields>
withCounts(const ByteReader& reader, SuperblockFields fields) {
	return inodeCount(reader).andThen([&](std::uint32_t inodes) {
		return blockCount(reader, fields.featureIncompat).map([&](std::uint64_t blocks) {
			fields.totalInodes = inodes;
			fields.totalBlocks = blocks;
			return fields;
		});
	});
}

[[nodiscard]] Result<SuperblockFields> readFields(const ByteReader& reader) {
	return withBlockSize(reader, SuperblockFields{})
		.andThen(std::bind_front(withFeatures, reader))
		.andThen(std::bind_front(withLayout, reader))
		.andThen(std::bind_front(withInodesPerGroup, reader))
		.andThen(std::bind_front(withRecordSizes, reader))
		.andThen(std::bind_front(withCounts, reader));
}

} // namespace

Result<Ext4Geometry> parseExt4Superblock(std::span<const std::byte> superblock) {
	if (superblock.size() < kSuperblockBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = superblock.size()};
	}
	const ByteReader reader{superblock.first(kSuperblockBytes)};
	return namesExt4(reader).andThen([&](bool) { return readFields(reader); }).andThen(geometryOf);
}

} // namespace revenant::fs::ext4
