// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::volume {

// Which of the two schemes described a disk.
enum class PartitionScheme : std::uint8_t { kMbr, kGpt };

// One partition, whichever scheme spelled it: where it is on the device, how
// long it is, the number an operator refers to it by, and the one line that
// lets them recognize it.
struct Partition {
	std::uint64_t startBytes = 0;
	std::uint64_t lengthBytes = 0;
	// One-based, in the order the table lists them. Stored rather than derived,
	// because it is what the operator types back: a number recomputed from a
	// filtered or reordered list would name a different partition than the one
	// it was read from.
	std::uint32_t number = 0;
	std::string label;
};

// A disk's layout, as read.
struct PartitionTable {
	PartitionScheme scheme = PartitionScheme::kMbr;
	std::vector<Partition> partitions;
	// True when the GPT's primary copy would not verify and the backup answered
	// (story-0404). Carried up because it says the disk is damaged.
	bool fromBackupHeader = false;
};

// Reads whichever partition table a device carries.
//
// Sector 0 decides: a table holding the protective entry hands the disk over to
// its GPT, and any other valid table is the answer itself. A sector 0 that will
// not parse is not the end of it — a wiped or overwritten first sector is one of
// the commonest things a damaged disk has, and the GPT that survives it is two
// sectors away — so the GPT is tried anyway. A device with neither is sector 0's
// own rejection, which is what an operator would look at first.
[[nodiscard]] Result<PartitionTable> readPartitionTable(BlockDevice& device);

} // namespace revenant::volume
