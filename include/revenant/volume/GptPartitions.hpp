// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/Gpt.hpp"

namespace revenant::volume {

// One partition as the table places it: absolute byte offsets on the device the
// table was read from, ready for a PartitionView. The type GUID and the name are
// what a user recognizes the partition by, and `nameIsExact` says whether the
// name survived decoding intact (ADR-0010).
struct GptPartition {
	std::uint64_t startBytes = 0;
	std::uint64_t lengthBytes = 0;
	std::array<std::byte, kGuidBytes> typeGuid{};
	std::string name;
	bool nameIsExact = true;
};

// A disk's GPT layout, and which of its two copies answered.
struct GptDisk {
	std::vector<GptPartition> partitions;
	// True when the primary header or its entry array would not verify and the
	// copy in the last sector was read instead. Reported rather than absorbed:
	// a disk that needed its backup is a damaged disk, and the operator should
	// know before they trust the rest of it.
	bool fromBackupHeader = false;
};

// Reads a device's GPT and returns every partition its entry array describes,
// in array order.
//
// The primary header at LBA 1 and the array it vouches for are tried first, as
// one unit: an array that fails its own CRC is exactly as unusable as a header
// that fails its. If any part of that fails, the header in the device's last
// sector and *its* array are tried instead.
//
// When neither copy verifies, the primary's rejection is what is returned — it
// names what an operator would look at first, and on a disk that never had a GPT
// it is the honest answer rather than a complaint about the last sector.
[[nodiscard]] Result<GptDisk> readGptPartitions(BlockDevice& device);

} // namespace revenant::volume
