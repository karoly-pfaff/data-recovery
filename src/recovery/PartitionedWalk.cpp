// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/PartitionedWalk.hpp"

#include <cstdint>
#include <span>
#include <string>

#include "core/SafeArith.hpp"
#include "recovery/VolumeWalk.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/volume/PartitionTable.hpp"
#include "revenant/volume/PartitionView.hpp"

namespace revenant::recovery {

namespace {

// Everything one partition reports, restated in the whole disk's coordinates.
//
// The volume that produced these was mounted on a window and has no idea it is
// one, so its offsets are relative to that window while everything downstream —
// the byte accounting, the carve gaps it produces, the extraction that reads
// those extents back — works in whole-disk offsets. Translating here is what
// keeps four filesystem parsers from having to know what a partition is.
//
// The path is qualified for a different reason: two volumes on one disk very
// often both hold `Users/`, and both are about to be written into one
// destination.
class PartitionEntries final : public fs::EntryVisitor {
public:
	PartitionEntries(fs::EntryVisitor& downstream, const volume::Partition& partition)
		: downstream_(&downstream), startBytes_(partition.startBytes),
		  prefix_("partition-" + std::to_string(partition.number) + "/") {}

	void onEntry(const fs::RecoveredEntry& entry) override {
		fs::RecoveredEntry moved = entry;
		moved.path = prefix_ + entry.path;
		for (fs::Extent& extent : moved.extents) {
			extent.deviceOffset = saturatingAdd64(extent.deviceOffset, startBytes_);
		}
		downstream_->onEntry(moved);
	}

private:
	fs::EntryVisitor* downstream_;
	std::uint64_t startBytes_;
	std::string prefix_;
};

// One disk's worth of walking, summed. A volume that would not mount contributes
// nothing and is not an error; `nonConformingVolume` is true if *any* volume set
// it, because an operator wants that raised by one bad volume out of four rather
// than averaged away.
[[nodiscard]] fs::EnumerationStats
plus(const fs::EnumerationStats& total, const fs::EnumerationStats& one) {
	return fs::EnumerationStats{
		.recordsScanned = total.recordsScanned + one.recordsScanned,
		.entriesReported = total.entriesReported + one.entriesReported,
		.nonConformingVolume = total.nonConformingVolume || one.nonConformingVolume};
}

[[nodiscard]] fs::EnumerationStats walkOne(
	BlockDevice& device,
	const volume::Partition& partition,
	fs::EntryVisitor& visitor,
	const fs::EnumerationStats& total) {
	volume::PartitionView view{device, partition.startBytes, partition.lengthBytes};
	PartitionEntries placed{visitor, partition};
	const auto walked = enumerateVolume(view, placed);
	if (!walked.hasValue()) {
		return total;
	}
	return plus(total, walked.value());
}

} // namespace

fs::EnumerationStats enumerateDisk(
	BlockDevice& device,
	std::span<const volume::Partition> partitions,
	fs::EntryVisitor& visitor) {
	fs::EnumerationStats total{
		.recordsScanned = 0,
		.entriesReported = 0,
		.nonConformingVolume = false};
	for (const volume::Partition& partition : partitions) {
		total = walkOne(device, partition, visitor, total);
	}
	return total;
}

} // namespace revenant::recovery
