// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal helpers shared by the exFAT boot-region parser's translation units:
// one reader per field, the raw block they fill, and the derivation that turns
// it into geometry. Each reader validates its own row and reports that field's
// byte offset on rejection. Not a public interface.

#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/exfat/BootRegion.hpp"

namespace revenant::fs::exfat {

// The offsets a rejection names.
inline constexpr std::uint64_t kFileSystemNameOffset = 0x03;
inline constexpr std::uint64_t kMustBeZeroOffset = 0x0B;
inline constexpr std::uint64_t kVolumeLengthOffset = 0x48;
inline constexpr std::uint64_t kFatOffsetOffset = 0x50;
inline constexpr std::uint64_t kFatLengthOffset = 0x54;
inline constexpr std::uint64_t kClusterHeapOffsetOffset = 0x58;
inline constexpr std::uint64_t kClusterCountOffset = 0x5C;
inline constexpr std::uint64_t kRootClusterOffset = 0x60;
inline constexpr std::uint64_t kSectorShiftOffset = 0x6C;
inline constexpr std::uint64_t kClusterShiftOffset = 0x6D;
inline constexpr std::uint64_t kFatCountOffset = 0x6E;

// What the boot sector says, in the sector counts it says it in, each field
// already validated on its own terms. The conversion to bytes is `geometryOf`.
struct BootRegion {
	std::uint32_t bytesPerSector;
	std::uint32_t sectorsPerCluster;
	std::uint32_t fatCount;
	std::uint64_t volumeSectors;
	std::uint64_t fatSector;
	std::uint64_t fatSectors;
	std::uint64_t clusterHeapSector;
	std::uint64_t clusterCount;
	std::uint32_t rootCluster;
};

// Whether the sector names exFAT *and* leaves FAT's geometry fields zero. Both
// halves matter: exFAT deliberately zeroes the 53 bytes a FAT BPB keeps its
// geometry in, so that no driver can mistake one for the other. This is the
// question the mount table probes with.
[[nodiscard]] Result<bool> namesExfat(const ByteReader& reader);

[[nodiscard]] Result<std::uint32_t> bytesPerSector(const ByteReader& reader);
[[nodiscard]] Result<std::uint32_t>
sectorsPerCluster(const ByteReader& reader, std::uint32_t sectorBytes);
[[nodiscard]] Result<std::uint32_t> fatCount(const ByteReader& reader);
[[nodiscard]] Result<bool> signatureIsValid(const ByteReader& reader);

// Restates a validated boot region as byte offsets, checking that the cluster
// heap fits inside the volume and that the root starts within it.
[[nodiscard]] Result<ExfatGeometry> geometryOf(const BootRegion& region);

} // namespace revenant::fs::exfat
