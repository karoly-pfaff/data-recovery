// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/exfat/BootRegion.hpp"

#include <concepts>
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

// Three of the steps are the same read at different addresses: a pair of
// fields, folded into two members. What differs is where the pair sits, how
// wide each of the two is, and where the values land — so that is all the three
// state. The addresses and the values both travel as named pairs rather than as
// two numbers in a row, which is two numbers waiting to be swapped.
struct FieldPair {
	std::uint64_t firstOffset;
	std::uint64_t secondOffset;
};

template <std::unsigned_integral First, std::unsigned_integral Second> struct FieldValues {
	First first;
	Second second;
};

template <std::unsigned_integral First, std::unsigned_integral Second, typename Fold>
[[nodiscard]] Result<BootRegion>
withPair(const ByteReader& reader, BootRegion region, FieldPair fields, Fold fold) {
	return reader.readLe<First>(fields.firstOffset).andThen([&](First first) {
		return reader.readLe<Second>(fields.secondOffset).map([&](Second second) {
			fold(region, FieldValues<First, Second>{.first = first, .second = second});
			return region;
		});
	});
}

[[nodiscard]] Result<BootRegion> withFatPlacement(const ByteReader& reader, BootRegion region) {
	return withPair<std::uint32_t, std::uint32_t>(
		reader,
		region,
		{.firstOffset = kFatOffsetOffset, .secondOffset = kFatLengthOffset},
		[](BootRegion& block, auto values) {
			block.fatSector = values.first;
			block.fatSectors = values.second;
		});
}

[[nodiscard]] Result<BootRegion> withHeap(const ByteReader& reader, BootRegion region) {
	return withPair<std::uint32_t, std::uint32_t>(
		reader,
		region,
		{.firstOffset = kClusterHeapOffsetOffset, .secondOffset = kClusterCountOffset},
		[](BootRegion& block, auto values) {
			block.clusterHeapSector = values.first;
			block.clusterCount = values.second;
		});
}

[[nodiscard]] Result<BootRegion> withVolume(const ByteReader& reader, BootRegion region) {
	return withPair<std::uint64_t, std::uint32_t>(
		reader,
		region,
		{.firstOffset = kVolumeLengthOffset, .secondOffset = kRootClusterOffset},
		[](BootRegion& block, auto values) {
			block.volumeSectors = values.first;
			block.rootCluster = values.second;
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
