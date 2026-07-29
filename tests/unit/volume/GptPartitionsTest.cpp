// SPDX-License-Identifier: GPL-3.0-or-later
// story-0404: a device's GPT read whole. The point of the format is that it
// keeps two checksummed copies of itself, so most of what is asserted here is
// that the second one is actually reached — and that reaching it is reported
// rather than absorbed.
#include "revenant/volume/GptPartitions.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "support/GptFixture.hpp"
#include "support/InMemoryDevice.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::testing::GptDiskShape;
using revenant::testing::gptTypeGuid;
using revenant::testing::InMemoryDevice;
using revenant::testing::invalidAt;
using revenant::testing::kFixtureArrayLba;
using revenant::testing::kFixtureFirstSectors;
using revenant::testing::kFixtureFirstStart;
using revenant::testing::kFixtureHeaderLba;
using revenant::testing::kFixtureSecondStart;
using revenant::testing::makeGptDisk;
using revenant::testing::outOfRangeAt;
using revenant::volume::readGptPartitions;

constexpr GptDiskShape kSmallSectors{.sectorSize = 512, .sectorCount = 4096};
constexpr GptDiskShape kLargeSectors{.sectorSize = 4096, .sectorCount = 512};

// Blanks the sector at `lba`, which is how a table copy stops verifying.
void eraseSector(std::vector<std::byte>& disk, const GptDiskShape& shape, std::uint64_t lba) {
	const auto at = static_cast<std::ptrdiff_t>(lba * shape.sectorSize);
	std::fill_n(disk.begin() + at, shape.sectorSize, std::byte{0});
}

TEST(GptPartitions, EveryUsedEntryBecomesAByteRange) {
	InMemoryDevice device{makeGptDisk(kSmallSectors), kSmallSectors.sectorSize};
	const auto found = readGptPartitions(device);
	ASSERT_TRUE(found.hasValue());
	ASSERT_EQ(found.value().partitions.size(), 2U);
	EXPECT_EQ(found.value().partitions.at(0).startBytes, kFixtureFirstStart * 512);
	EXPECT_EQ(found.value().partitions.at(0).lengthBytes, kFixtureFirstSectors * 512);
	EXPECT_EQ(found.value().partitions.at(0).name, std::string{"System"});
	EXPECT_EQ(found.value().partitions.at(1).startBytes, kFixtureSecondStart * 512);
	EXPECT_FALSE(found.value().fromBackupHeader);
}

// An entry states an inclusive last sector, so its length counts that sector in.
TEST(GptPartitions, ATypeGuidSurvivesTheReadUnchanged) {
	InMemoryDevice device{makeGptDisk(kSmallSectors), kSmallSectors.sectorSize};
	const auto found = readGptPartitions(device);
	ASSERT_TRUE(found.hasValue());
	ASSERT_EQ(found.value().partitions.size(), 2U);
	EXPECT_TRUE(std::ranges::equal(found.value().partitions.at(0).typeGuid, gptTypeGuid(0xA1)));
	EXPECT_TRUE(std::ranges::equal(found.value().partitions.at(1).typeGuid, gptTypeGuid(0xB2)));
}

TEST(GptPartitions, ByteOffsetsScaleWithTheDevicesSectorSize) {
	InMemoryDevice device{makeGptDisk(kLargeSectors), kLargeSectors.sectorSize};
	const auto found = readGptPartitions(device);
	ASSERT_TRUE(found.hasValue());
	ASSERT_EQ(found.value().partitions.size(), 2U);
	EXPECT_EQ(found.value().partitions.at(0).startBytes, kFixtureFirstStart * 4096);
	EXPECT_EQ(found.value().partitions.at(0).lengthBytes, kFixtureFirstSectors * 4096);
}

// The reason GPT keeps a second copy, and the situation a recovery tool is for.
TEST(GptPartitions, ADestroyedPrimaryHeaderIsAnsweredFromTheLastSector) {
	auto disk = makeGptDisk(kSmallSectors);
	eraseSector(disk, kSmallSectors, kFixtureHeaderLba);
	InMemoryDevice device{disk, kSmallSectors.sectorSize};
	const auto found = readGptPartitions(device);
	ASSERT_TRUE(found.hasValue());
	EXPECT_EQ(found.value().partitions.size(), 2U);
	EXPECT_TRUE(found.value().fromBackupHeader);
}

// An array that fails its own checksum is exactly as unusable as a header that
// fails its, so it sends the read to the other copy too.
TEST(GptPartitions, APrimaryEntryArrayThatDoesNotChecksumFallsBackAsWell) {
	auto disk = makeGptDisk(kSmallSectors);
	eraseSector(disk, kSmallSectors, kFixtureArrayLba);
	InMemoryDevice device{disk, kSmallSectors.sectorSize};
	const auto found = readGptPartitions(device);
	ASSERT_TRUE(found.hasValue());
	EXPECT_EQ(found.value().partitions.size(), 2U);
	EXPECT_TRUE(found.value().fromBackupHeader);
}

TEST(GptPartitions, ADestroyedBackupStillLeavesThePrimaryReadable) {
	auto disk = makeGptDisk(kSmallSectors);
	eraseSector(disk, kSmallSectors, kSmallSectors.sectorCount - 1);
	InMemoryDevice device{disk, kSmallSectors.sectorSize};
	const auto found = readGptPartitions(device);
	ASSERT_TRUE(found.hasValue());
	EXPECT_FALSE(found.value().fromBackupHeader);
}

// The primary's rejection is the one reported: on a disk that never had a GPT it
// is the honest answer, rather than a complaint about the last sector.
TEST(GptPartitions, WhenNeitherCopyVerifiesThePrimarysRejectionIsReported) {
	auto disk = makeGptDisk(kSmallSectors);
	eraseSector(disk, kSmallSectors, kFixtureHeaderLba);
	eraseSector(disk, kSmallSectors, kSmallSectors.sectorCount - 1);
	InMemoryDevice device{disk, kSmallSectors.sectorSize};
	EXPECT_EQ(revenant::testing::rejectionOf(readGptPartitions(device)), invalidAt(0x00));
}

TEST(GptPartitions, ADeviceWithNoRoomForATableIsOutOfRange) {
	const std::vector<std::byte> stub(300, std::byte{0});
	InMemoryDevice device{stub, 512};
	EXPECT_EQ(revenant::testing::rejectionOf(readGptPartitions(device)), outOfRangeAt(stub.size()));
}

} // namespace
