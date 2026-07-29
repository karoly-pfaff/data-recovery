// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/volume/MbrPartitions.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/Mbr.hpp"
#include "volume/MbrInternal.hpp"
#include "volume/MbrLayout.hpp"
#include "volume/SectorIo.hpp"

namespace revenant::volume {

namespace {

// One primary slot together with where its type byte sat, which is what a
// refusal has to name.
struct PrimarySlot {
	MbrEntry entry{};
	std::uint64_t typeOffset = 0;
};

[[nodiscard]] Result<MbrPartition>
primaryPartition(const BlockDevice& device, const MbrEntry& entry) {
	const PlacedEntry placed{
		.startLba = entry.startLba,
		.sectorCount = entry.sectorCount,
		.typeCode = entry.type,
		.logical = false};
	return partitionOf(placed, device.sectorSize());
}

// A slot that describes something real: the logical partitions of its chain if
// it is a container, and itself if it is not.
[[nodiscard]] Result<std::vector<MbrPartition>>
oneOrChain(BlockDevice& device, const MbrEntry& entry) {
	if (isExtendedType(entry.type)) {
		return logicalPartitions(device, entry.startLba);
	}
	return primaryPartition(device, entry).map([](const MbrPartition& partition) {
		return std::vector<MbrPartition>{partition};
	});
}

// What one primary slot contributes: nothing for an unused slot, a refusal for
// a protective one — every byte of a GPT disk belongs to the GPT, so there is
// nothing here to report and something elsewhere to read — and its own extent
// or its chain's for anything else.
[[nodiscard]] Result<std::vector<MbrPartition>>
contributionOf(BlockDevice& device, const PrimarySlot& slot) {
	if (slot.entry.type == kUnusedPartitionType) {
		return std::vector<MbrPartition>{};
	}
	if (slot.entry.type == kProtectivePartitionType) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = slot.typeOffset};
	}
	return oneOrChain(device, slot.entry);
}

[[nodiscard]] PrimarySlot slotOf(const MbrTable& table, std::size_t index) {
	return PrimarySlot{
		.entry = table.entries.at(index),
		.typeOffset = slotOffset(index) + kTypeField};
}

[[nodiscard]] Result<std::vector<MbrPartition>> appendSlot(
	BlockDevice& device,
	const MbrTable& table,
	std::vector<MbrPartition> found,
	std::size_t index) {
	return contributionOf(device, slotOf(table, index))
		.map([&found](const std::vector<MbrPartition>& contributed) {
			found.insert(found.end(), contributed.begin(), contributed.end());
			return found;
		});
}

// The four slots folded into one list, in table order. A slot that refuses ends
// the read: a partition list missing the entry that objected would be a worse
// answer than none.
[[nodiscard]] Result<std::vector<MbrPartition>>
partitionsOf(BlockDevice& device, const MbrTable& table) {
	Result<std::vector<MbrPartition>> found{std::vector<MbrPartition>{}};
	for (std::size_t index = 0; index < kMbrEntryCount; ++index) {
		found = found.andThen([&](const std::vector<MbrPartition>& built) {
			return appendSlot(device, table, built, index);
		});
	}
	return found;
}

} // namespace

Result<std::vector<MbrPartition>> readMbrPartitions(BlockDevice& device) {
	return readTableSector(device, 0)
		.andThen([](const TableSector& sector) { return parseMbrSector(sector); })
		.andThen([&device](const MbrTable& table) { return partitionsOf(device, table); });
}

} // namespace revenant::volume
