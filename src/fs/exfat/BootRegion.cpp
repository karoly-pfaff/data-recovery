// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/exfat/BootRegion.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

#include "BootRegionInternal.hpp"
#include "fs/MountRegion.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::exfat {

namespace {

// Each step folds one pair of validated fields into the block under
// construction, so a rejection anywhere short-circuits the whole read. The
// pairing is by nesting budget, not by meaning.

[[nodiscard]] Result<BootRegion> withClusterSize(const ByteReader& reader, BootRegion region) {
	return bytesPerSector(reader).andThen([&](std::uint32_t sectorBytes) {
		return sectorsPerCluster(reader, sectorBytes).map([&](std::uint32_t clusterSectors) {
			region.bytesPerSector = sectorBytes;
			region.sectorsPerCluster = clusterSectors;
			return region;
		});
	});
}

// Two of the steps are the same read with different addresses: a pair of 32-bit
// fields, folded into two members. What differs between them is where the pair
// sits and where the values land, so that is all they state. Both the addresses
// and the values travel as named pairs rather than as two numbers in a row,
// which is two numbers waiting to be swapped.
struct FieldPair {
	std::uint64_t firstOffset;
	std::uint64_t secondOffset;
};

struct FieldValues {
	std::uint32_t first;
	std::uint32_t second;
};

using FoldPair = void (*)(BootRegion&, FieldValues);

[[nodiscard]] Result<BootRegion>
withPair(const ByteReader& reader, BootRegion region, FieldPair fields, FoldPair fold) {
	return reader.readLe<std::uint32_t>(fields.firstOffset).andThen([&](std::uint32_t first) {
		return reader.readLe<std::uint32_t>(fields.secondOffset).map([&](std::uint32_t second) {
			fold(region, FieldValues{.first = first, .second = second});
			return region;
		});
	});
}

[[nodiscard]] Result<BootRegion> withFatPlacement(const ByteReader& reader, BootRegion region) {
	return withPair(
		reader,
		region,
		{.firstOffset = kFatOffsetOffset, .secondOffset = kFatLengthOffset},
		[](BootRegion& block, FieldValues values) {
			block.fatSector = values.first;
			block.fatSectors = values.second;
		});
}

[[nodiscard]] Result<BootRegion> withHeap(const ByteReader& reader, BootRegion region) {
	return withPair(
		reader,
		region,
		{.firstOffset = kClusterHeapOffsetOffset, .secondOffset = kClusterCountOffset},
		[](BootRegion& block, FieldValues values) {
			block.clusterHeapSector = values.first;
			block.clusterCount = values.second;
		});
}

[[nodiscard]] Result<BootRegion> withVolume(const ByteReader& reader, BootRegion region) {
	return reader.readLe<std::uint64_t>(kVolumeLengthOffset).andThen([&](std::uint64_t sectors) {
		return reader.readLe<std::uint32_t>(kRootClusterOffset).map([&](std::uint32_t root) {
			region.volumeSectors = sectors;
			region.rootCluster = root;
			return region;
		});
	});
}

[[nodiscard]] Result<BootRegion> withFatCount(const ByteReader& reader, BootRegion region) {
	return fatCount(reader).map([&](std::uint32_t count) {
		region.fatCount = count;
		return region;
	});
}

[[nodiscard]] Result<BootRegion> readBootRegion(const ByteReader& reader) {
	return withClusterSize(reader, BootRegion{})
		.andThen(std::bind_front(withFatPlacement, reader))
		.andThen(std::bind_front(withHeap, reader))
		.andThen(std::bind_front(withVolume, reader))
		.andThen(std::bind_front(withFatCount, reader));
}

} // namespace

Result<ExfatGeometry> parseExfatBootSector(std::span<const std::byte> sector) {
	if (sector.size() < kBootSectorBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = sector.size()};
	}
	const ByteReader reader{sector.first(kBootSectorBytes)};
	return namesExfat(reader)
		.andThen([&](bool) { return signatureIsValid(reader); })
		.andThen([&](bool) { return readBootRegion(reader); })
		.andThen(geometryOf);
}

} // namespace revenant::fs::exfat
