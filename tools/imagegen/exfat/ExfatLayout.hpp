// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant::imagegen::exfat {

// The geometry of the synthetic exFAT volume. Every derived offset is computed
// here, so no builder and no test spells out a byte position of its own.
struct ExfatLayout {
	std::uint32_t bytesPerSector;
	std::uint32_t sectorsPerCluster;
	std::uint32_t volumeSectors;
	std::uint32_t fatSector;
	std::uint32_t fatSectors;
	std::uint32_t heapSector;
	std::uint32_t clusterCount;
	std::uint32_t rootCluster;

	[[nodiscard]] std::uint32_t bytesPerCluster() const noexcept;
	[[nodiscard]] std::uint64_t fatOffsetBytes() const noexcept;
	[[nodiscard]] std::uint64_t heapOffsetBytes() const noexcept;
	[[nodiscard]] std::uint64_t clusterOffsetBytes(std::uint32_t cluster) const noexcept;
	[[nodiscard]] std::uint64_t totalBytes() const noexcept;
};

// The one fixed plan the whole fixture is built from.
[[nodiscard]] ExfatLayout makeExfatLayout() noexcept;

// Where the fixture's files live. Spelled out so the fragmentation of
// `keep.txt`, and the fact that the deleted files are contiguous, are visible
// layout decisions rather than something a builder derived.
inline constexpr std::uint32_t kRootCluster = 2;
inline constexpr std::uint32_t kBitmapCluster = 3;
inline constexpr std::uint32_t kKeepCluster = 10;
inline constexpr std::uint32_t kKeepSecondCluster = 20;
inline constexpr std::uint32_t kDeletedCluster = 30;
inline constexpr std::uint32_t kPhotosCluster = 40;
inline constexpr std::uint32_t kInnerCluster = 50;
// The volume handed this one out again after the file was deleted, so what is
// there now is not what the entry claims.
inline constexpr std::uint32_t kOverwrittenCluster = 60;

} // namespace revenant::imagegen::exfat
