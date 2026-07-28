// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"

namespace revenant::fs::exfat {

// Cluster numbering starts at 2, as it does in FAT: entries 0 and 1 of the FAT
// are reserved and hold no file.
inline constexpr std::uint32_t kFirstDataCluster = 2;

// Validated exFAT geometry. Every on-disk field behind these has been checked
// and every derivation overflow-tested, so the values are safe to use directly
// as byte offsets and sizes.
struct ExfatGeometry {
	std::uint32_t bytesPerSector;
	std::uint32_t bytesPerCluster;
	std::uint32_t fatCount;
	// Where the *first* FAT begins; a second, if there is one, follows it.
	std::uint64_t fatOffsetBytes;
	// One FAT, not all of them.
	std::uint64_t fatSizeBytes;
	// Where cluster `kFirstDataCluster` begins.
	std::uint64_t clusterHeapOffsetBytes;
	std::uint64_t totalClusters;
	std::uint32_t rootCluster;
};

// Parses and validates the main boot sector of an exFAT volume.
//
// Truncated input yields kOutOfRange; any on-disk rule violation yields
// kInvalidArgument carrying the offending field's byte offset; arithmetic that
// would wrap yields kOverflow.
//
// exFAT states its geometry as log2 exponents rather than counts, so the
// exponents are range-checked *before* anything is shifted by them: an
// unchecked shift is undefined behaviour, not a large number.
[[nodiscard]] Result<ExfatGeometry> parseExfatBootSector(std::span<const std::byte> sector);

} // namespace revenant::fs::exfat
