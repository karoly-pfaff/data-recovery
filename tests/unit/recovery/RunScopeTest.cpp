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

// How many partitions `readPartitionTable` finds inside a device — the question
// the old shape asked of a window and this one does not. Asked here so the
// phantom fixture is pinned to genuinely posing it.
[[nodiscard]] std::size_t tableEntriesIn(revenant::BlockDevice& device) {
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
// same breath: partition 1's first sector really does parse as a table naming a
// partition — those bytes are a real volume's bootstrap area — and the scope
// still resolves to the window and stops there. Under the old shape the engine
// walked that phantom table, mounted none of it, and reported an intact
// filesystem with no files in it.
TEST(RunScope, AVolumeWhoseFirstSectorParsesAsATableIsStillOneVolume) {
	InMemoryDevice device{buildPhantomTableDiskImage().bytes, kSector};
	auto scope = RunScope::resolve(device, kNtfsPartition);
	ASSERT_TRUE(scope.hasValue());
	ASSERT_GT(tableEntriesIn(scope.value().device()), 0U);
	EXPECT_TRUE(scope.value().layout().empty());
}

// And the negative that keeps the pair honest: the clean fixture poses no such
// question, so a phantom builder that quietly stopped writing its table would
// fail here rather than leave the case above passing for nothing.
TEST(RunScope, TheCleanFixtureCarriesNoTableInsideThatPartition) {
	InMemoryDevice device{buildMbrDiskImage().bytes, kSector};
	auto scope = RunScope::resolve(device, kNtfsPartition);
	ASSERT_TRUE(scope.hasValue());
	EXPECT_EQ(tableEntriesIn(scope.value().device()), 0U);
}

} // namespace
