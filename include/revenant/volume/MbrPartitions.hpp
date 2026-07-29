// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::volume {

// How many logical partitions one extended partition may contribute before the
// walk stops believing its chain (ADR-0009 bounded allocation). Real disks hold
// a handful; the largest number any tool has ever had to handle is far below
// this, so a chain that reaches the cap is a corrupt or crafted one.
inline constexpr std::size_t kMaxLogicalPartitions = 128;

// One partition as the table places it: absolute byte offsets on the device the
// table was read from, ready for a PartitionView. `typeCode` is the table's own
// vocabulary — 0x07 for NTFS/exFAT, 0x83 for Linux, and so on — kept because it
// is what a user recognizes their disk by.
struct MbrPartition {
	std::uint64_t startBytes = 0;
	std::uint64_t lengthBytes = 0;
	std::uint8_t typeCode = 0;
	// Whether this came from the EBR chain rather than the primary table. The
	// distinction survives into the report because it is how a user names the
	// partition they mean.
	bool logical = false;
};

// Reads sector 0 of `device` and returns every partition its table describes,
// in table order, with each extended entry replaced by the logical partitions
// its EBR chain holds.
//
// A sector 0 that will not read or does not hold a valid table is that failure's
// typed error; a device reporting a zero sector size is kInvalidArgument. A
// table holding a protective (0xEE) entry is kInvalidArgument at that entry's
// type byte — the disk's real layout is in its GPT, not here.
//
// A broken *chain* is not an error: an unreadable, invalid or off-device link
// ends the walk and the partitions found before it are returned.
[[nodiscard]] Result<std::vector<MbrPartition>> readMbrPartitions(BlockDevice& device);

} // namespace revenant::volume
