// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "revenant/core/Result.hpp"

namespace revenant::volume {

// The primary header sits in the sector after the protective MBR; the backup
// copy sits in the last sector of the disk.
inline constexpr std::uint64_t kPrimaryHeaderLba = 1;

// The smallest header the specification allows. A larger one is legal and its
// extra bytes are covered by the same checksum.
inline constexpr std::size_t kGptHeaderBytes = 92;

// Every GUID here — a partition's type, the disk's own — is sixteen bytes.
inline constexpr std::size_t kGuidBytes = 16;

// The smallest entry the specification allows; a larger one pads the same
// fields. Every implementation in the wild writes exactly this.
inline constexpr std::size_t kGptEntryBytes = 128;

// How large an entry array this parser will read (ADR-0009 bounded allocation).
// The array's size is stated by the very bytes it is supposed to validate, so
// the product is bounded before anything is allocated. 128 entries of 128 bytes
// is what every tool writes; this leaves room for four hundred times that.
inline constexpr std::size_t kMaxEntryArrayBytes = 1U << 22U;

// A validated GPT header. Every field behind these has been checked against the
// header's own CRC32 first, so they are safe to act on.
struct GptHeader {
	std::uint64_t myLba = 0;
	std::uint64_t alternateLba = 0;
	std::uint64_t firstUsableLba = 0;
	std::uint64_t lastUsableLba = 0;
	std::uint64_t entryArrayLba = 0;
	std::uint32_t entryCount = 0;
	std::uint32_t entryBytes = 0;
	std::uint32_t entryArrayCrc = 0;
};

// One partition entry. `lastLba` is inclusive, as the format states it. The
// unique GUID and the attribute bits are not read: nothing in recovery asks
// them anything.
struct GptEntry {
	std::array<std::byte, kGuidBytes> typeGuid{};
	std::uint64_t firstLba = 0;
	std::uint64_t lastLba = 0;
	std::string name;
	bool nameIsExact = true;
};

// Parses and validates the header a sector holds, against the LBA it was read
// from.
//
// Too few bytes yields kOutOfRange; any on-disk rule violation yields
// kInvalidArgument carrying the offending field's byte offset.
//
// The checksum is verified before any other field is believed: everything else
// in the header is a number this parser will act on, and the CRC is the only
// evidence those numbers were written rather than landed there. `atLba` is
// checked against the header's own `MyLBA`, which is what keeps a copy found in
// the wrong place from being read as if it belonged there.
[[nodiscard]] Result<GptHeader>
parseGptHeader(std::span<const std::byte> sector, std::uint64_t atLba);

// Parses one entry slot. Too few bytes yields kOutOfRange; a last LBA below the
// first yields kInvalidArgument at 0x28. An unused slot parses successfully —
// it is a value, not a failure — and `isUnusedEntry` recognizes it.
[[nodiscard]] Result<GptEntry> parseGptEntry(std::span<const std::byte> slot);

// Whether the slot describes nothing. GPT has no deleted state and no tombstone:
// an entry either names a partition type or is sixteen zero bytes.
[[nodiscard]] bool isUnusedEntry(const GptEntry& entry) noexcept;

} // namespace revenant::volume
