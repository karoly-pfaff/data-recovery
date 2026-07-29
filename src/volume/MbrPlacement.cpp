// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>

#include "fs/SafeArith.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/volume/MbrPartitions.hpp"
#include "volume/MbrInternal.hpp"

namespace revenant::volume {

namespace {

// A container's type byte. The first two are DOS's, differing only in whether
// the CHS or the LBA fields were meant to be believed; the third is the one
// Linux writes, and a disk carrying it is as real as either.
constexpr std::uint8_t kExtendedChsType = 0x05;
constexpr std::uint8_t kExtendedLbaType = 0x0F;
constexpr std::uint8_t kLinuxExtendedType = 0x85;

} // namespace

bool isExtendedType(std::uint8_t type) noexcept {
	return type == kExtendedChsType || type == kExtendedLbaType || type == kLinuxExtendedType;
}

Result<MbrPartition> partitionOf(const PlacedEntry& placed, std::uint32_t sectorSize) {
	return fs::safeMul64(placed.startLba, sectorSize, /*offset=*/0)
		.map([&placed, sectorSize](std::uint64_t startBytes) {
			return MbrPartition{
				.startBytes = startBytes,
				.lengthBytes = static_cast<std::uint64_t>(placed.sectorCount) * sectorSize,
				.typeCode = placed.typeCode,
				.logical = placed.logical};
		});
}

} // namespace revenant::volume
