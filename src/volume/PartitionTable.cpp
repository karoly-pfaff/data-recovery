// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/volume/PartitionTable.hpp"

#include <cstdint>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/GptPartitions.hpp"
#include "revenant/volume/Mbr.hpp"
#include "revenant/volume/MbrPartitions.hpp"
#include "volume/PartitionLabel.hpp"
#include "volume/SectorIo.hpp"

namespace revenant::volume {

namespace {

[[nodiscard]] Partition partitionFrom(const MbrPartition& entry, std::uint32_t number) {
	return Partition{
		.startBytes = entry.startBytes,
		.lengthBytes = entry.lengthBytes,
		.number = number,
		.label = labelOfMbrType(entry.typeCode)};
}

[[nodiscard]] Partition partitionFrom(const GptPartition& entry, std::uint32_t number) {
	return Partition{
		.startBytes = entry.startBytes,
		.lengthBytes = entry.lengthBytes,
		.number = number,
		.label = labelOfGptPartition(entry)};
}

// The scheme's own partitions, numbered from one in the order it lists them.
template <typename Entry>
[[nodiscard]] std::vector<Partition> numbered(const std::vector<Entry>& entries) {
	std::vector<Partition> partitions;
	std::uint32_t number = 0;
	for (const Entry& entry : entries) {
		++number;
		partitions.push_back(partitionFrom(entry, number));
	}
	return partitions;
}

[[nodiscard]] Result<PartitionTable> gptTable(BlockDevice& device) {
	return readGptPartitions(device).map([](const GptDisk& disk) {
		return PartitionTable{
			.scheme = PartitionScheme::kGpt,
			.partitions = numbered(disk.partitions),
			.fromBackupHeader = disk.fromBackupHeader};
	});
}

[[nodiscard]] Result<PartitionTable> mbrTable(BlockDevice& device) {
	return readMbrPartitions(device).map([](const std::vector<MbrPartition>& entries) {
		return PartitionTable{
			.scheme = PartitionScheme::kMbr,
			.partitions = numbered(entries),
			.fromBackupHeader = false};
	});
}

// The GPT, or `fallback` when there is none. Reached from two directions: a
// sector 0 that will not parse at all, and one that parses but names nothing.
// A GPT disk whose sector 0 was overwritten looks like the first and one whose
// protective entry was cleared like the second, and in both the real table may
// still be two sectors away.
[[nodiscard]] Result<PartitionTable>
gptOr(BlockDevice& device, const Result<PartitionTable>& fallback) {
	auto gpt = gptTable(device);
	if (gpt.hasValue()) {
		return gpt;
	}
	return fallback;
}

[[nodiscard]] Result<PartitionTable> tableFor(BlockDevice& device, const MbrTable& sectorZero) {
	if (defersToGpt(sectorZero)) {
		return gptTable(device);
	}
	auto mbr = mbrTable(device);
	if (mbr.hasValue() && !mbr.value().partitions.empty()) {
		return mbr;
	}
	return gptOr(device, mbr);
}

[[nodiscard]] Result<MbrTable> readSectorZero(BlockDevice& device) {
	return readTableSector(device, 0).andThen(
		[](const TableSector& sector) { return parseMbrSector(sector); });
}

} // namespace

Result<PartitionTable> readPartitionTable(BlockDevice& device) {
	const auto sectorZero = readSectorZero(device);
	if (!sectorZero.hasValue()) {
		return gptOr(device, sectorZero.error());
	}
	return tableFor(device, sectorZero.value());
}

} // namespace revenant::volume
