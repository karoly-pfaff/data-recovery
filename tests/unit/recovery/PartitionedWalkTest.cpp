// SPDX-License-Identifier: GPL-3.0-or-later
// story-0407: a whole disk walked as all of its volumes. The two things asserted
// throughout are the ones everything downstream depends on — that an extent comes
// back in the *disk's* coordinates rather than its partition's, and that two
// volumes holding the same path do not collide.
//
// story-0610 narrowed the seam: the partitions to walk arrive from the caller,
// so these cases hand in what the disk's table describes rather than leaving
// the walk to find it. The unpartitioned case moved to `RunScopeTest`, where
// the decision now lives.
#include "recovery/PartitionedWalk.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "imagegen/disk/DiskImageBuilder.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/volume/PartitionTable.hpp"
#include "support/CollectingEntryVisitor.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::recovery::enumerateDisk;
using revenant::testing::CollectingEntryVisitor;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kSector = 512;

[[nodiscard]] bool anyPathStartsWith(
	const std::vector<revenant::fs::RecoveredEntry>& entries,
	const std::string& prefix) {
	return std::ranges::any_of(entries, [&prefix](const revenant::fs::RecoveredEntry& entry) {
		return entry.path.starts_with(prefix);
	});
}

// What a resolved scope hands the walk. Read here rather than taken from a
// `RunScope` so that what these cases exercise is the walking, not the
// deciding.
[[nodiscard]] std::vector<revenant::volume::Partition> tableOf(revenant::BlockDevice& device) {
	auto table = revenant::volume::readPartitionTable(device);
	if (!table.hasValue()) {
		ADD_FAILURE() << "the fixture disk carries no readable partition table";
		return {};
	}
	return table.value().partitions;
}

TEST(PartitionedWalk, APartitionedDiskReportsMoreThanOneVolumesEntries) {
	InMemoryDevice device{revenant::imagegen::disk::buildMbrDiskImage().bytes, kSector};
	CollectingEntryVisitor visitor;
	const auto walked = enumerateDisk(device, tableOf(device), visitor);
	EXPECT_GT(walked.entriesReported, 0U);
	EXPECT_TRUE(anyPathStartsWith(visitor.entries(), "partition-1/"));
	EXPECT_TRUE(anyPathStartsWith(visitor.entries(), "partition-2/"));
}

// Without the prefix, the second volume's `photos/deleted.jpg` would land on the
// first one's in the destination.
TEST(PartitionedWalk, EveryPathCarriesThePartitionItCameFrom) {
	InMemoryDevice device{revenant::imagegen::disk::buildMbrDiskImage().bytes, kSector};
	CollectingEntryVisitor visitor;
	EXPECT_GT(enumerateDisk(device, tableOf(device), visitor).entriesReported, 0U);
	const bool allQualified =
		std::ranges::all_of(visitor.entries(), [](const revenant::fs::RecoveredEntry& entry) {
			return entry.path.starts_with("partition-");
		});
	EXPECT_TRUE(allQualified);
}

// The volume reports offsets relative to its own window; everything downstream
// works in whole-disk offsets. An extent still inside the first megabyte would
// mean the translation never happened.
TEST(PartitionedWalk, ExtentsComeBackInTheDisksOwnCoordinates) {
	const auto disk = revenant::imagegen::disk::buildMbrDiskImage();
	InMemoryDevice device{disk.bytes, kSector};
	CollectingEntryVisitor visitor;
	EXPECT_GT(enumerateDisk(device, tableOf(device), visitor).entriesReported, 0U);
	const auto firstStart = disk.volumeOffsets.at(0);
	const bool allPlaced = std::ranges::all_of(visitor.entries(), [firstStart](const auto& entry) {
		return std::ranges::all_of(entry.extents, [firstStart](const revenant::fs::Extent& at) {
			return at.deviceOffset >= firstStart;
		});
	});
	EXPECT_TRUE(allPlaced);
}

TEST(PartitionedWalk, SumsWhatEveryPartitionScanned) {
	InMemoryDevice device{revenant::imagegen::disk::buildMbrDiskImage().bytes, kSector};
	CollectingEntryVisitor visitor;
	const auto walked = enumerateDisk(device, tableOf(device), visitor);
	EXPECT_EQ(walked.entriesReported, visitor.entries().size());
	EXPECT_GT(walked.recordsScanned, 0U);
}

// A swap partition, an EFI system partition this build cannot read, a volume
// whose superblock is gone: all normal on a real disk, and none a reason to
// abandon the volumes that did mount.
TEST(PartitionedWalk, APartitionThatWillNotMountDoesNotStopTheDisk) {
	auto disk = revenant::imagegen::disk::buildMbrDiskImage();
	const auto second = static_cast<std::ptrdiff_t>(disk.volumeOffsets.at(1));
	std::fill_n(disk.bytes.begin() + second, kSector, std::byte{0});
	InMemoryDevice device{disk.bytes, kSector};
	CollectingEntryVisitor visitor;
	EXPECT_GT(enumerateDisk(device, tableOf(device), visitor).entriesReported, 0U);
	EXPECT_TRUE(anyPathStartsWith(visitor.entries(), "partition-1/"));
}

} // namespace
