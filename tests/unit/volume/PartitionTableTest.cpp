// SPDX-License-Identifier: GPL-3.0-or-later
// story-0045: one reading of a disk's layout, whichever scheme wrote it. What
// is asserted here is mostly the fall-through: a wiped or cleared sector 0 is
// one of the commonest things a damaged disk has, and the GPT that survives it
// is two sectors away.
#include "revenant/volume/PartitionTable.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "support/GptFixture.hpp"
#include "support/InMemoryDevice.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::toLittleEndian;
using revenant::testing::GptDiskShape;
using revenant::testing::InMemoryDevice;
using revenant::testing::invalidAt;
using revenant::testing::kFixtureFirstStart;
using revenant::testing::makeGptDisk;
using revenant::volume::PartitionScheme;
using revenant::volume::readPartitionTable;

constexpr std::uint32_t kSector = 512;
constexpr std::size_t kDiskSectors = 4096;
constexpr std::size_t kTableOffset = 0x1BE;
constexpr std::size_t kEntryBytes = 16;
constexpr std::size_t kSignatureOffset = 0x1FE;
constexpr std::uint8_t kNtfsType = 0x07;
constexpr std::uint8_t kLinuxType = 0x83;

constexpr GptDiskShape kShape{.sectorSize = kSector, .sectorCount = kDiskSectors};

void writeLe(std::vector<std::byte>& disk, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, disk.begin() + static_cast<std::ptrdiff_t>(offset));
}

// Which slot to fill and what to call it. Named rather than passed as two
// integers, so no caller can put the type where the index goes.
struct Slot {
	std::size_t index = 0;
	std::uint8_t type = 0;
};

void writeSlot(std::vector<std::byte>& disk, const Slot& slot) {
	const auto at = kTableOffset + (slot.index * kEntryBytes);
	writeLe(disk, at + 0x04, slot.type);
	writeLe(disk, at + 0x08, static_cast<std::uint32_t>(2048 + (slot.index * 1024)));
	writeLe(disk, at + 0x0C, static_cast<std::uint32_t>(1024));
}

[[nodiscard]] std::vector<std::byte> makeMbrDisk() {
	std::vector<std::byte> disk(kDiskSectors * kSector, std::byte{0});
	writeLe(disk, kSignatureOffset, static_cast<std::uint16_t>(0xAA55U));
	writeSlot(disk, Slot{.index = 0, .type = kNtfsType});
	writeSlot(disk, Slot{.index = 1, .type = kLinuxType});
	return disk;
}

// The first sector of a GPT disk, blanked — the commonest way a disk arrives
// looking unpartitioned while its real table is still intact.
[[nodiscard]] std::vector<std::byte> makeGptDiskWithWipedSectorZero() {
	auto disk = makeGptDisk(kShape);
	std::fill_n(disk.begin(), kSector, std::byte{0});
	return disk;
}

TEST(PartitionTable, ANormalTableIsReadAsAnMbr) {
	InMemoryDevice device{makeMbrDisk(), kSector};
	const auto table = readPartitionTable(device);
	ASSERT_TRUE(table.hasValue());
	EXPECT_EQ(table.value().scheme, PartitionScheme::kMbr);
	ASSERT_EQ(table.value().partitions.size(), 2U);
	EXPECT_EQ(table.value().partitions.at(0).startBytes, 2048ULL * kSector);
	EXPECT_EQ(table.value().partitions.at(0).label, std::string{"NTFS/exFAT"});
}

TEST(PartitionTable, PartitionsAreNumberedFromOneInTableOrder) {
	InMemoryDevice device{makeMbrDisk(), kSector};
	const auto table = readPartitionTable(device);
	ASSERT_TRUE(table.hasValue());
	ASSERT_EQ(table.value().partitions.size(), 2U);
	EXPECT_EQ(table.value().partitions.at(0).number, 1U);
	EXPECT_EQ(table.value().partitions.at(1).number, 2U);
}

TEST(PartitionTable, AProtectiveTableSendsTheReadToTheGpt) {
	InMemoryDevice device{makeGptDisk(kShape), kSector};
	const auto table = readPartitionTable(device);
	ASSERT_TRUE(table.hasValue());
	EXPECT_EQ(table.value().scheme, PartitionScheme::kGpt);
	ASSERT_EQ(table.value().partitions.size(), 2U);
	EXPECT_EQ(table.value().partitions.at(0).startBytes, kFixtureFirstStart * kSector);
	EXPECT_EQ(table.value().partitions.at(0).label, std::string{"System"});
}

// A hybrid table carries the guard entry alongside real ones. The GPT is still
// the complete answer, and reading the hybrid's own entries would hand back a
// subset of it.
TEST(PartitionTable, AHybridTableSendsTheReadToTheGptAsWell) {
	auto disk = makeGptDisk(kShape);
	writeSlot(disk, Slot{.index = 1, .type = kNtfsType});
	InMemoryDevice device{disk, kSector};
	const auto table = readPartitionTable(device);
	ASSERT_TRUE(table.hasValue());
	EXPECT_EQ(table.value().scheme, PartitionScheme::kGpt);
	EXPECT_EQ(table.value().partitions.size(), 2U);
}

TEST(PartitionTable, AWipedSectorZeroStillFindsTheGpt) {
	InMemoryDevice device{makeGptDiskWithWipedSectorZero(), kSector};
	const auto table = readPartitionTable(device);
	ASSERT_TRUE(table.hasValue());
	EXPECT_EQ(table.value().scheme, PartitionScheme::kGpt);
	EXPECT_EQ(table.value().partitions.size(), 2U);
}

TEST(PartitionTable, ADamagedGptIsReportedAsHavingUsedItsBackup) {
	auto disk = makeGptDisk(kShape);
	std::fill_n(disk.begin() + kSector, kSector, std::byte{0});
	InMemoryDevice device{disk, kSector};
	const auto table = readPartitionTable(device);
	ASSERT_TRUE(table.hasValue());
	EXPECT_TRUE(table.value().fromBackupHeader);
}

// Sector 0's own rejection is the one reported: it is what an operator would
// look at first, and on a disk that never had a table it is the whole story.
TEST(PartitionTable, ADiskWithNeitherSchemeIsSectorZerosRejection) {
	const std::vector<std::byte> blank(kDiskSectors * kSector, std::byte{0});
	InMemoryDevice device{blank, kSector};
	EXPECT_EQ(
		revenant::testing::rejectionOf(readPartitionTable(device)),
		invalidAt(kSignatureOffset));
}

} // namespace
