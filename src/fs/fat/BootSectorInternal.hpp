// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal helpers shared by the FAT32 boot-sector parser's translation units:
// one reader per FAT32-specific BPB field, the raw block they fill, and the
// derivation that turns it into geometry. Each reader validates its own row and
// reports that field's byte offset on rejection; the fields FAT32 shares with
// NTFS come from fs/BpbFields.hpp. Not a public interface.

#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/fat/BootSector.hpp"

namespace revenant::fs::fat {

// The FAT32-specific fields' byte offsets, named here so a derivation that
// blames one of them does not restate the number.
inline constexpr std::uint64_t kReservedSectorsOffset = 0x0E;
inline constexpr std::uint64_t kFatCountOffset = 0x10;
inline constexpr std::uint64_t kTotalSectorsOffset = 0x20;
inline constexpr std::uint64_t kFatSizeOffset = 0x24;
inline constexpr std::uint64_t kRootClusterOffset = 0x2C;
inline constexpr std::uint64_t kFilSysTypeOffset = 0x52;

// What the BPB says, each field validated on its own terms and none of them yet
// combined. Sector counts, not byte offsets — the conversion is `geometryFrom`.
struct Bpb {
	std::uint32_t bytesPerSector;
	std::uint32_t sectorsPerCluster;
	std::uint32_t reservedSectors;
	std::uint32_t fatCount;
	std::uint64_t fatSectors;
	std::uint64_t totalSectors;
	std::uint32_t rootCluster;
};

// Whether the boot sector names FAT32 in `BS_FilSysType`. This is the marker
// the mount table probes with, so recognition and parsing cannot drift apart:
// both ask this one question.
[[nodiscard]] Result<bool> filSysTypeIsFat32(const ByteReader& reader);

// The three BPB fields that exist only on FAT12/16. On FAT32 each must be zero;
// whichever is not is what the rejection names.
[[nodiscard]] Result<bool> fat16OnlyFieldsAreZero(const ByteReader& reader);

[[nodiscard]] Result<std::uint32_t> reservedSectors(const ByteReader& reader);
[[nodiscard]] Result<std::uint32_t> fatCount(const ByteReader& reader);
[[nodiscard]] Result<std::uint64_t> fatSectors(const ByteReader& reader);
[[nodiscard]] Result<std::uint64_t> totalSectors(const ByteReader& reader);
[[nodiscard]] Result<std::uint32_t> rootCluster(const ByteReader& reader);

// Restates a validated BPB as byte offsets and cluster counts, checking that
// the data region fits inside the volume and that the root starts within it.
[[nodiscard]] Result<Fat32Geometry> geometryFrom(const Bpb& bpb);

} // namespace revenant::fs::fat
