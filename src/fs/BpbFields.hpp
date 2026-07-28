// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. NTFS and FAT32 both open with a BIOS parameter block, and the
// fields they share carry the same rules in both — so the rules live here once
// rather than once per filesystem. Each reader validates its own field and
// reports that field's byte offset on rejection.
//
// exFAT is deliberately not a client: it states its geometry as log2 exponents
// in different fields, which is a different rule, not a shared one.

#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs {

// The shared fields' byte offsets, named here so a derivation that blames one
// of them does not restate the number.
inline constexpr std::uint64_t kBytesPerSectorOffset = 0x0B;
inline constexpr std::uint64_t kSectorsPerClusterOffset = 0x0D;
inline constexpr std::uint64_t kBootSignatureOffset = 0x1FE;

// 512, 1024, 2048 or 4096.
[[nodiscard]] Result<std::uint32_t> bytesPerSector(const ByteReader& reader);

// A power of two from 1 to 128.
[[nodiscard]] Result<std::uint32_t> sectorsPerCluster(const ByteReader& reader);

// The 0x55 0xAA that closes a boot sector.
[[nodiscard]] Result<bool> bootSignatureIsValid(const ByteReader& reader);

} // namespace revenant::fs
