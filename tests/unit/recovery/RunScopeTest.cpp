// SPDX-License-Identifier: GPL-3.0-or-later
// story-0610: what byte range a run works in and how its filesystem pass must
// read it, decided once from one reading of the source's table. The case this
// unit exists for is the last pair — a volume whose own first sector parses as a
// partition table is still one volume, because nothing looks inside a window.
#include "revenant/recovery/RunScope.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include "imagegen/disk/DiskImageBuilder.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/PartitionTable.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::ErrorCode;
using revenant::imagegen::disk::buildMbrDiskImage;
using revenant::imagegen::disk::buildPhantomTableDiskImage;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::recovery::kWholeSource;
using revenant::recovery::RunScope;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kSector = 512;

// The NTFS fixture, which is where the phantom table is written.
constexpr std::uint32_t kNtfsPartition = 1;

// The four fixture volumes the synthetic disk's table describes.
constexpr std::size_t kFixturePartitions = 4;

// Enough of a device to answer both the sector-zero read and the GPT probes
// behind it, all of them zeros — a source carrying no table at all.
constexpr std::size_t kUntabledBytes = std::size_t{64} * 1024;

[[nodiscard]] InMemoryDevice untabledDevice() {
	return InMemoryDevice{std::vector<std::byte>(kUntabledBytes, std::byte{0}), kSector};
}

// One sector, read at `offset`. A window's placement is checked by what it
// actually reads, not by asking it where it thinks it is.
[[nodiscard]] std::vector<std::byte> sectorAt(revenant::BlockDevice& device, std::uint64_t offset) {
	std::vector<std::byte> sector(kSector, std::byte{0});
	EXPECT_TRUE(device.readAt(offset, sector).hasValue());
	return sector;
}

// How many partitions `readPartitionTable` names inside a device — the question
// the old shape asked of a window and this one does not. A table that will not
// read and a table that names nothing both count zero, because what the old
// shape did with either was the same: fall back to walking the device whole.
[[nodiscard]] std::size_t partitionsNamedIn(revenant::BlockDevice& device) {
	const auto table = revenant::volume::readPartitionTable(device);
	return table.hasValue() ? table.value().partitions.size() : 0;
}

TEST(RunScope, AWholeDiskResolvesToEveryPartitionItsTableDescribes) {
	InMemoryDevice device{buildMbrDiskImage().bytes, kSector};
	auto scope = RunScope::resolve(device, kWholeSource);
	ASSERT_TRUE(scope.hasValue());
	EXPECT_EQ(&scope.value().device(), &device);
	EXPECT_EQ(scope.value().layout().size(), kFixturePartitions);
}

// The window is stated against the entry the whole-disk scope reports, not
// against arithmetic the test does itself: what is under test is that a scoped
// run works in exactly the range the table names.
TEST(RunScope, ANamedPartitionResolvesToItsWindowWalkedAsOneVolume) {
	InMemoryDevice device{buildMbrDiskImage().bytes, kSector};
	auto whole = RunScope::resolve(device, kWholeSource);
	ASSERT_TRUE(whole.hasValue());
	// The *second* entry, not the first: a resolver that always handed back the
	// head of the table would pass a test written against partition 1.
	const revenant::volume::Partition second = *std::next(whole.value().layout().begin());
	ASSERT_EQ(second.number, 2U);

	auto scope = RunScope::resolve(device, second.number);
	ASSERT_TRUE(scope.hasValue());
	EXPECT_EQ(sectorAt(scope.value().device(), 0), sectorAt(device, second.startBytes));
	EXPECT_EQ(scope.value().device().sizeInBytes(), second.lengthBytes);
	EXPECT_TRUE(scope.value().layout().empty());
}

TEST(RunScope, ANumberTheTableDoesNotCarryIsRefused) {
	InMemoryDevice device{buildMbrDiskImage().bytes, kSector};
	const auto scope = RunScope::resolve(device, 9);
	ASSERT_FALSE(scope.hasValue());
	EXPECT_EQ(scope.error().code, ErrorCode::kNotFound);
}

// story-0407's single-volume fallback, moved to where the table it is a
// decision about is held. An image of one volume is the ordinary case.
TEST(RunScope, AnImageOfOneVolumeResolvesToThatVolume) {
	InMemoryDevice device{buildNtfsImage(), kSector};
	auto scope = RunScope::resolve(device, kWholeSource);
	ASSERT_TRUE(scope.hasValue());
	EXPECT_TRUE(scope.value().layout().empty());
	EXPECT_EQ(&scope.value().device(), &device);
}

TEST(RunScope, ASourceWithNoReadableTableIsStillWalkedWhole) {
	InMemoryDevice device = untabledDevice();
	auto scope = RunScope::resolve(device, kWholeSource);
	ASSERT_TRUE(scope.hasValue());
	EXPECT_TRUE(scope.value().layout().empty());
	EXPECT_EQ(&scope.value().device(), &device);
}

// The other half of the same decision: nothing was named, so nothing can be
// guessed. Recovering the wrong range is worse than recovering nothing.
TEST(RunScope, ASourceWithNoReadableTableRefusesAScopedRun) {
	InMemoryDevice device = untabledDevice();
	EXPECT_FALSE(RunScope::resolve(device, 3).hasValue());
}

// The audit's case, with the fixture's own adversarial property pinned in the
// same breath — the `ASSERT` is what fails if the phantom builder ever stops
// writing a table: partition 1's first sector really does name a partition, and
// those bytes are what a real volume's bootstrap area is. The scope resolves to
// the window and stops there. Under the old shape the engine walked that
// phantom table, mounted none of it, and reported an intact filesystem with no
// files in it.
TEST(RunScope, AVolumeWhoseFirstSectorParsesAsATableIsStillOneVolume) {
	InMemoryDevice device{buildPhantomTableDiskImage().bytes, kSector};
	auto scope = RunScope::resolve(device, kNtfsPartition);
	ASSERT_TRUE(scope.hasValue());
	ASSERT_GT(partitionsNamedIn(scope.value().device()), 0U);
	EXPECT_TRUE(scope.value().layout().empty());
}

// The control the case above is read against: the same partition of the clean
// fixture names nothing, so "the phantom disk differs from the clean one by
// exactly this" is a measured statement rather than an assumed one. Its table
// still *parses* — a zero-filled bootstrap area is a valid MBR with four unused
// slots, which is why the suite was green before this story.
TEST(RunScope, TheCleanFixturesSamePartitionNamesNoPartitions) {
	InMemoryDevice device{buildMbrDiskImage().bytes, kSector};
	auto scope = RunScope::resolve(device, kNtfsPartition);
	ASSERT_TRUE(scope.hasValue());
	EXPECT_EQ(partitionsNamedIn(scope.value().device()), 0U);
}

} // namespace
