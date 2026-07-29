// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"

namespace revenant::volume {

// The master boot record occupies the first 512 bytes of the disk whatever the
// device's sector size is: boot code, then four 16-byte partition entries, then
// the two-byte signature.
inline constexpr std::size_t kMbrSectorBytes = 512;

// The table has exactly four slots. Everything past the fourth partition lives
// in the EBR chain hanging off an extended entry.
inline constexpr std::size_t kMbrEntryCount = 4;

// A slot whose type byte is zero describes nothing; the rest of its bytes are
// whatever the last writer left there.
inline constexpr std::uint8_t kUnusedPartitionType = 0x00;

// The type a protective MBR gives its single entry: the disk is GPT-partitioned
// and a tool that only understands this table must keep away from it.
inline constexpr std::uint8_t kProtectivePartitionType = 0xEE;

// One 16-byte slot, in the units it states itself in. `startLba` is absolute for
// a slot in sector 0 and relative for one in an EBR, which is the chain walk's
// knowledge rather than this parser's. The CHS fields are deliberately not read:
// they cannot address a modern disk and their own writers fill them with a
// placeholder.
struct MbrEntry {
	std::uint8_t type = kUnusedPartitionType;
	std::uint32_t startLba = 0;
	std::uint32_t sectorCount = 0;
};

// The four slots, each already validated on its own terms.
struct MbrTable {
	std::array<MbrEntry, kMbrEntryCount> entries{};
};

// Parses and validates the partition table in a 512-byte sector-0 image — or in
// an EBR, which is the same sector shape.
//
// Truncated input yields kOutOfRange; any on-disk rule violation yields
// kInvalidArgument carrying the offending field's byte offset.
//
// The status byte of every slot is checked and then discarded. Recovery does not
// care which partition was bootable, but requiring all four to be 0x00 or 0x80
// is what distinguishes a real table from the boot sector of an unpartitioned
// volume, which carries the same 0x55AA signature over unrelated boot code.
[[nodiscard]] Result<MbrTable> parseMbrSector(std::span<const std::byte> sector);

} // namespace revenant::volume
