// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/exfat/ExfatLayout.hpp"

#include <cstdint>

namespace revenant::imagegen::exfat {

namespace {

constexpr std::uint32_t kFirstDataCluster = 2;

} // namespace

std::uint32_t ExfatLayout::bytesPerCluster() const noexcept {
	return bytesPerSector * sectorsPerCluster;
}

std::uint64_t ExfatLayout::fatOffsetBytes() const noexcept {
	return std::uint64_t{fatSector} * bytesPerSector;
}

std::uint64_t ExfatLayout::heapOffsetBytes() const noexcept {
	return std::uint64_t{heapSector} * bytesPerSector;
}

std::uint64_t ExfatLayout::clusterOffsetBytes(std::uint32_t cluster) const noexcept {
	return heapOffsetBytes() + (std::uint64_t{cluster - kFirstDataCluster} * bytesPerCluster());
}

std::uint64_t ExfatLayout::totalBytes() const noexcept {
	return std::uint64_t{volumeSectors} * bytesPerSector;
}

ExfatLayout makeExfatLayout() noexcept {
	return ExfatLayout{
		.bytesPerSector = 512,
		.sectorsPerCluster = 8,
		.volumeSectors = 4096,
		.fatSector = 128,
		.fatSectors = 64,
		.heapSector = 256,
		.clusterCount = 480,
		.rootCluster = kRootCluster};
}

} // namespace revenant::imagegen::exfat
