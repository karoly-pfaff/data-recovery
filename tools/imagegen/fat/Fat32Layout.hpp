// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant::imagegen::fat {

// The geometry of the synthetic FAT32 volume. Every derived offset is computed
// here, so no builder and no test spells out a byte position of its own.
struct Fat32Layout {
	std::uint32_t bytesPerSector;
	std::uint32_t sectorsPerCluster;
	std::uint32_t reservedSectors;
	std::uint32_t fatCount;
	std::uint32_t fatSectors;
	std::uint32_t totalSectors;
	std::uint32_t rootCluster;

	[[nodiscard]] std::uint32_t bytesPerCluster() const noexcept;
	[[nodiscard]] std::uint64_t fatOffsetBytes(std::uint32_t fat) const noexcept;
	[[nodiscard]] std::uint64_t fatSizeBytes() const noexcept;
	[[nodiscard]] std::uint64_t dataOffsetBytes() const noexcept;
	[[nodiscard]] std::uint64_t clusterOffsetBytes(std::uint32_t cluster) const noexcept;
	[[nodiscard]] std::uint64_t totalBytes() const noexcept;
};

// The one fixed plan the whole fixture is built from. It takes no arguments on
// purpose: a fixture whose shape can vary is one no test can assert against.
[[nodiscard]] Fat32Layout makeFat32Layout() noexcept;

// Where the fixture's files live. Spelled out here so the fragmentation of
// `keep-photo.jpg` — and the fact that every deleted file is contiguous, which
// is the only thing an undelete can assume — are visible layout decisions.
inline constexpr std::uint32_t kRootCluster = 2;
inline constexpr std::uint32_t kPhotosCluster = 3;
inline constexpr std::uint32_t kNotesCluster = 5;
inline constexpr std::uint32_t kDeletedNotesCluster = 6;
inline constexpr std::uint32_t kGoneDirCluster = 8;
inline constexpr std::uint32_t kKeepJpegCluster = 10;
inline constexpr std::uint32_t kKeepJpegSecondCluster = 20;
inline constexpr std::uint32_t kDeletedJpegCluster = 30;
inline constexpr std::uint32_t kOrphanJpegCluster = 40;
// No directory entry points at this one; it is what the carve pass is for.
inline constexpr std::uint32_t kUnallocatedJpegCluster = 60;

} // namespace revenant::imagegen::fat
