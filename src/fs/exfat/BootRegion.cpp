// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/exfat/BootRegion.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

#include "BootRegionInternal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::exfat {

namespace {

constexpr std::size_t kBootSectorBytes = 512;

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

[[nodiscard]] Result<BootRegion> withFatPlacement(const ByteReader& reader, BootRegion region) {
	return reader.readLe<std::uint32_t>(kFatOffsetOffset).andThen([&](std::uint32_t first) {
		return reader.readLe<std::uint32_t>(kFatLengthOffset).map([&](std::uint32_t length) {
			region.fatSector = first;
			region.fatSectors = length;
			return region;
		});
	});
}

[[nodiscard]] Result<BootRegion> withHeap(const ByteReader& reader, BootRegion region) {
	return reader.readLe<std::uint32_t>(kClusterHeapOffsetOffset).andThen([&](std::uint32_t heap) {
		return reader.readLe<std::uint32_t>(kClusterCountOffset).map([&](std::uint32_t count) {
			region.clusterHeapSector = heap;
			region.clusterCount = count;
			return region;
		});
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
