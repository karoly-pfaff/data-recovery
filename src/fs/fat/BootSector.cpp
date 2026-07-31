// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/fat/BootSector.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

#include "BootSectorInternal.hpp"
#include "fs/BpbFields.hpp"
#include "fs/MountRegion.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::fat {

namespace {

// Each step folds one pair of validated fields into the block under
// construction, so a rejection anywhere short-circuits the whole read. The
// pairing is by nesting budget, not by meaning: two `andThen`s is as deep as a
// step is allowed to go.

[[nodiscard]] Result<Bpb> withClusterSize(const ByteReader& reader, Bpb bpb) {
	return bytesPerSector(reader).andThen([&](std::uint32_t sectorBytes) {
		return sectorsPerCluster(reader).map([&](std::uint32_t clusterSectors) {
			bpb.bytesPerSector = sectorBytes;
			bpb.sectorsPerCluster = clusterSectors;
			return bpb;
		});
	});
}

[[nodiscard]] Result<Bpb> withFatPlacement(const ByteReader& reader, Bpb bpb) {
	return reservedSectors(reader).andThen([&](std::uint32_t reserved) {
		return fatCount(reader).map([&](std::uint32_t count) {
			bpb.reservedSectors = reserved;
			bpb.fatCount = count;
			return bpb;
		});
	});
}

[[nodiscard]] Result<Bpb> withFatSize(const ByteReader& reader, Bpb bpb) {
	return fatSectors(reader).map([&](std::uint64_t sectors) {
		bpb.fatSectors = sectors;
		return bpb;
	});
}

[[nodiscard]] Result<Bpb> withVolume(const ByteReader& reader, Bpb bpb) {
	return totalSectors(reader).andThen([&](std::uint64_t sectors) {
		return rootCluster(reader).map([&](std::uint32_t root) {
			bpb.totalSectors = sectors;
			bpb.rootCluster = root;
			return bpb;
		});
	});
}

[[nodiscard]] Result<Bpb> readBpb(const ByteReader& reader) {
	return withClusterSize(reader, Bpb{})
		.andThen(std::bind_front(withFatPlacement, reader))
		.andThen(std::bind_front(withFatSize, reader))
		.andThen(std::bind_front(withVolume, reader));
}

} // namespace

Result<Fat32Geometry> parseFat32BootSector(std::span<const std::byte> sector) {
	if (sector.size() < kBootSectorBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = sector.size()};
	}
	const ByteReader reader{sector.first(kBootSectorBytes)};
	return filSysTypeIsFat32(reader)
		.andThen([&](bool) { return fat16OnlyFieldsAreZero(reader); })
		.andThen([&](bool) { return bootSignatureIsValid(reader); })
		.andThen([&](bool) { return readBpb(reader); })
		.andThen(geometryFrom);
}

} // namespace revenant::fs::fat
