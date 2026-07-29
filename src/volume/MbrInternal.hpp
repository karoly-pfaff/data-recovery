// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. What the MBR scheme knows beyond the shape of one sector: which
// type bytes name a container rather than a partition, how a slot's stated
// sectors become byte offsets on the device, and how the EBR chain behind an
// extended entry is walked. Not a public interface.

#include <cstdint>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/MbrPartitions.hpp"

namespace revenant::volume {

// One slot's extent with its relative addressing already resolved: `startLba` is
// where the partition actually begins on the device, whichever base the slot
// stated it against.
struct PlacedEntry {
	std::uint64_t startLba = 0;
	std::uint32_t sectorCount = 0;
	std::uint8_t typeCode = 0;
	bool logical = false;
};

// Whether `type` names a container rather than a partition: DOS extended with
// CHS addressing (0x05), the same with LBA addressing (0x0F), and the Linux
// extended type (0x85) that a Linux-partitioned disk really does use.
[[nodiscard]] bool isExtendedType(std::uint8_t type) noexcept;

// `placed` restated in bytes, at `sectorSize` bytes per sector. The length needs
// no overflow check — a 32-bit count of 32-bit-sized sectors always fits 64 bits
// — but the start does, because a resolved start is no longer bounded by 32 bits.
[[nodiscard]] Result<MbrPartition> partitionOf(const PlacedEntry& placed, std::uint32_t sectorSize);

// Every logical partition the EBR chain rooted at `extendedStartLba` describes,
// in chain order. A link that will not read, will not parse, or leaves the
// device ends the walk and what was found before it is returned — a corrupt tail
// must not cost the caller the head (as in fs::ext4::orphanInodes).
[[nodiscard]] std::vector<MbrPartition>
logicalPartitions(BlockDevice& device, std::uint32_t extendedStartLba);

} // namespace revenant::volume
