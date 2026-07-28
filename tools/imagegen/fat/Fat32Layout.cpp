// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/fat/Fat32Layout.hpp"

#include <cstdint>

namespace revenant::imagegen::fat {

namespace {

// Cluster numbering starts at 2, so the data region's first cluster is 2 and
// not 0. Duplicating the parser's constant would couple the fixture to it; the
// number is the format's, and it is stated once here.
constexpr std::uint32_t kFirstDataCluster = 2;

} // namespace

std::uint32_t Fat32Layout::bytesPerCluster() const noexcept {
	return bytesPerSector * sectorsPerCluster;
}

std::uint64_t Fat32Layout::fatOffsetBytes(std::uint32_t fat) const noexcept {
	return (std::uint64_t{reservedSectors} * bytesPerSector) + (fat * fatSizeBytes());
}

std::uint64_t Fat32Layout::fatSizeBytes() const noexcept {
	return std::uint64_t{fatSectors} * bytesPerSector;
}

std::uint64_t Fat32Layout::dataOffsetBytes() const noexcept {
	const auto dataStartSector = reservedSectors + (fatCount * fatSectors);
	return std::uint64_t{dataStartSector} * bytesPerSector;
}

std::uint64_t Fat32Layout::clusterOffsetBytes(std::uint32_t cluster) const noexcept {
	return dataOffsetBytes() + (std::uint64_t{cluster - kFirstDataCluster} * bytesPerCluster());
}

std::uint64_t Fat32Layout::totalBytes() const noexcept {
	return std::uint64_t{totalSectors} * bytesPerSector;
}

Fat32Layout makeFat32Layout() noexcept {
	return Fat32Layout{
		.bytesPerSector = 512,
		.sectorsPerCluster = 4,
		.reservedSectors = 32,
		.fatCount = 2,
		.fatSectors = 64,
		.totalSectors = 4096,
		.rootCluster = kRootCluster};
}

} // namespace revenant::imagegen::fat
