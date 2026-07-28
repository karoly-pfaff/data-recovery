// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"

namespace revenant::fs::fat {

// Cluster numbering starts at 2: FAT entries 0 and 1 hold the media descriptor
// and the volume's dirty flags, so no file ever lives in them.
inline constexpr std::uint32_t kFirstDataCluster = 2;

// Validated FAT32 geometry. Every on-disk field behind these has been checked
// and every derivation overflow-tested, so the values are safe to use directly
// as byte offsets and sizes.
struct Fat32Geometry {
	std::uint32_t bytesPerSector;
	std::uint32_t bytesPerCluster;
	std::uint32_t fatCount;
	// Where the *first* FAT begins; the others follow it, `fatSizeBytes` apart.
	std::uint64_t fatOffsetBytes;
	// One FAT, not all of them.
	std::uint64_t fatSizeBytes;
	// Where cluster `kFirstDataCluster` begins.
	std::uint64_t dataOffsetBytes;
	// Data clusters only: valid cluster numbers run from `kFirstDataCluster` to
	// `totalClusters + kFirstDataCluster - 1`.
	std::uint64_t totalClusters;
	std::uint32_t rootCluster;
};

// Parses and validates the FAT32 BPB in a 512-byte boot sector. Truncated input
// yields kOutOfRange; any on-disk rule violation yields kInvalidArgument
// carrying the offending field's byte offset; arithmetic that would wrap yields
// kOverflow.
//
// The `FAT32   ` type string is checked like any other field. Turning that
// particular rejection into the mount table's "not my filesystem" is the
// mounter's job, not the parser's.
[[nodiscard]] Result<Fat32Geometry> parseFat32BootSector(std::span<const std::byte> sector);

} // namespace revenant::fs::fat
