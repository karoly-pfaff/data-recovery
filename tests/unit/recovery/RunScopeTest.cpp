// SPDX-License-Identifier: GPL-3.0-or-later
// story-0610: what byte range a run works in and how its filesystem pass must
// read it, decided once from one reading of the source's table. The case this
// unit exists for is the last one — a volume whose own first sector parses as a
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
#include "revenant/volume/PartitionTable.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::ErrorCode;
using revenant::imagegen::disk::buildMbrDiskImage;
using revenant::imagegen::disk::buildPhantomTableDiskImage;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::recovery::RunScope;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kSector = 512;

// Zero is not a partition number, so it names the source itself.
constexpr std::uint32_t kWholeSource = 0;

// The four fixture volumes the synthetic disk's table describes.
constexpr std::size_t kFixturePartitions = 4;

// Enough of a device to answer both the sector-zero read and the GPT probes
// behind it, all of them zeros — a source carrying no table at all.
constexpr std::size_t kUntabledBytes = std::size_t{64} * 1024;

[[nodiscard]] InMemoryDevice untabledDevice() {
	return InMemoryDevice{std::vector<std::byte>(kUntabledBytes, std::byte{0}), kSector};
}

TEST(RunScope, AWholeDiskResolvesToEveryPartitionItsTableDescribes) {
	InMemoryDevice device{buildMbrDiskImage().bytes, kSector};
	auto scope = RunScope::resolve(device, kWholeSource);
	ASSERT_TRUE(scope.hasValue());
	EXPECT_EQ(scope.value().startBytes(), 0U);
	EXPECT_EQ(scope.value().layout().size(), kFixturePartitions);
	EXPECT_EQ(scope.value().device().sizeInBytes(), device.sizeInBytes());
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
	EXPECT_EQ(scope.value().startBytes(), second.startBytes);
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
	EXPECT_EQ(scope.value().startBytes(), 0U);
	EXPECT_EQ(scope.value().device().sizeInBytes(), device.sizeInBytes());
}

TEST(RunScope, ASourceWithNoReadableTableIsStillWalkedWhole) {
	InMemoryDevice device = untabledDevice();
	auto scope = RunScope::resolve(device, kWholeSource);
	ASSERT_TRUE(scope.hasValue());
	EXPECT_TRUE(scope.value().layout().empty());
	EXPECT_EQ(scope.value().device().sizeInBytes(), device.sizeInBytes());
}

// The other half of the same decision: nothing was named, so nothing can be
// guessed. Recovering the wrong range is worse than recovering nothing.
TEST(RunScope, ASourceWithNoReadableTableRefusesAScopedRun) {
	InMemoryDevice device = untabledDevice();
	EXPECT_FALSE(RunScope::resolve(device, 3).hasValue());
}

// The audit's case. Partition 1's own first sector parses as a valid MBR — it
// is a real volume's bootstrap area, which is what those bytes are. The scope
// resolves to the window and stops there; asking a volume whether it is a disk
// is what turned an intact filesystem into a carve-only scan.
TEST(RunScope, AVolumeWhoseFirstSectorParsesAsATableIsStillOneVolume) {
	InMemoryDevice device{buildPhantomTableDiskImage().bytes, kSector};
	auto scope = RunScope::resolve(device, 1);
	ASSERT_TRUE(scope.hasValue());
	EXPECT_TRUE(scope.value().layout().empty());
}

} // namespace
