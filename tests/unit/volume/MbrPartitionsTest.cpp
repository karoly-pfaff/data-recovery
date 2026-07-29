// SPDX-License-Identifier: GPL-3.0-or-later
// story-0403: sector 0 read off a device and turned into byte ranges. The EBR
// chain is where the value is: its two relative addresses are stated against
// different bases, and a chain read from untrusted bytes has to be bounded both
// by length and by revisit before it is followed at all.
#include "revenant/volume/MbrPartitions.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "support/InMemoryDevice.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::toLittleEndian;
using revenant::testing::InMemoryDevice;
using revenant::testing::invalidAt;
using revenant::testing::outOfRangeAt;
using revenant::volume::kMaxLogicalPartitions;
using revenant::volume::readMbrPartitions;

constexpr std::uint32_t kSector = 512;
constexpr std::size_t kDiskSectors = 4096;
constexpr std::size_t kTableOffset = 0x1BE;
constexpr std::size_t kEntryBytes = 16;
constexpr std::size_t kSignatureOffset = 0x1FE;
constexpr std::uint16_t kSignature = 0xAA55;

constexpr std::uint8_t kNtfsType = 0x07;
constexpr std::uint8_t kLinuxType = 0x83;
constexpr std::uint8_t kExtendedType = 0x05;
constexpr std::uint8_t kProtectiveType = 0xEE;

// The extended partition every chain test hangs its EBRs inside, and how far
// past its own EBR each logical partition begins.
constexpr std::uint32_t kExtendedStart = 1000;
constexpr std::uint32_t kExtendedSectors = 1000;
constexpr std::uint32_t kEbrGap = 63;

struct Slot {
	std::uint8_t type = 0;
	std::uint32_t startLba = 0;
	std::uint32_t sectorCount = 0;
};

// Which sector's table a slot belongs to, and which of its four slots it is.
struct SlotAt {
	std::uint64_t lba = 0;
	std::size_t index = 0;
};

// One link of an EBR chain. `nextRelative` is stated against the extended
// partition's head, as the format requires; zero ends the chain.
struct EbrLink {
	std::uint64_t lba = 0;
	std::uint32_t sectors = 0;
	std::uint32_t nextRelative = 0;
};

void writeLe(std::vector<std::byte>& disk, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, disk.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] std::size_t slotBase(const SlotAt& at) {
	return static_cast<std::size_t>(at.lba * kSector) + kTableOffset + (at.index * kEntryBytes);
}

// The status byte is left at 0x00, which is one of the two values a table may
// hold there.
void writeSlot(std::vector<std::byte>& disk, const SlotAt& at, const Slot& slot) {
	const auto base = slotBase(at);
	writeLe(disk, base + 0x04, slot.type);
	writeLe(disk, base + 0x08, slot.startLba);
	writeLe(disk, base + 0x0C, slot.sectorCount);
}

void signTable(std::vector<std::byte>& disk, std::uint64_t lba) {
	writeLe(disk, static_cast<std::size_t>(lba * kSector) + kSignatureOffset, kSignature);
}

[[nodiscard]] std::vector<std::byte> makeDisk() {
	std::vector<std::byte> disk(kDiskSectors * kSector, std::byte{0});
	signTable(disk, 0);
	return disk;
}

[[nodiscard]] std::vector<std::byte> makeTwoPartitionDisk() {
	auto disk = makeDisk();
	writeSlot(
		disk,
		SlotAt{.lba = 0, .index = 0},
		Slot{.type = kNtfsType, .startLba = 2048, .sectorCount = 1024});
	writeSlot(
		disk,
		SlotAt{.lba = 0, .index = 1},
		Slot{.type = kLinuxType, .startLba = 3072, .sectorCount = 512});
	return disk;
}

void writeExtendedEntry(std::vector<std::byte>& disk) {
	writeSlot(
		disk,
		SlotAt{.lba = 0, .index = 0},
		Slot{.type = kExtendedType, .startLba = kExtendedStart, .sectorCount = kExtendedSectors});
}

void writeEbr(std::vector<std::byte>& disk, const EbrLink& link) {
	signTable(disk, link.lba);
	writeSlot(
		disk,
		SlotAt{.lba = link.lba, .index = 0},
		Slot{.type = kLinuxType, .startLba = kEbrGap, .sectorCount = link.sectors});
	if (link.nextRelative != 0) {
		writeSlot(
			disk,
			SlotAt{.lba = link.lba, .index = 1},
			Slot{.type = kExtendedType, .startLba = link.nextRelative, .sectorCount = 1});
	}
}

// Three links. The third is reachable only from the extended partition's head:
// 1000 + 800. A walk that stated slot 1 against the current EBR would look at
// 1500 + 800 instead, find nothing, and stop one partition short.
[[nodiscard]] std::vector<std::byte> makeChainDisk() {
	auto disk = makeDisk();
	writeExtendedEntry(disk);
	writeEbr(disk, EbrLink{.lba = 1000, .sectors = 100, .nextRelative = 500});
	writeEbr(disk, EbrLink{.lba = 1500, .sectors = 200, .nextRelative = 800});
	writeEbr(disk, EbrLink{.lba = 1800, .sectors = 300, .nextRelative = 0});
	return disk;
}

[[nodiscard]] std::uint64_t byteOffsetOf(std::uint64_t lba) {
	return lba * kSector;
}

TEST(MbrPartitions, EveryUsedPrimarySlotBecomesAByteRange) {
	InMemoryDevice device{makeTwoPartitionDisk(), kSector};
	const auto found = readMbrPartitions(device);
	ASSERT_TRUE(found.hasValue());
	ASSERT_EQ(found.value().size(), 2U);
	EXPECT_EQ(found.value().at(0).startBytes, byteOffsetOf(2048));
	EXPECT_EQ(found.value().at(0).lengthBytes, byteOffsetOf(1024));
	EXPECT_EQ(found.value().at(0).typeCode, kNtfsType);
	EXPECT_FALSE(found.value().at(0).logical);
	EXPECT_EQ(found.value().at(1).startBytes, byteOffsetOf(3072));
}

// The table states sectors; what a sector is belongs to the device.
TEST(MbrPartitions, ByteOffsetsScaleWithTheDevicesSectorSize) {
	InMemoryDevice device{makeTwoPartitionDisk(), 4096};
	const auto found = readMbrPartitions(device);
	ASSERT_TRUE(found.hasValue());
	ASSERT_EQ(found.value().size(), 2U);
	EXPECT_EQ(found.value().at(0).startBytes, 2048ULL * 4096ULL);
	EXPECT_EQ(found.value().at(0).lengthBytes, 1024ULL * 4096ULL);
}

TEST(MbrPartitions, AnExtendedEntryContributesItsChainInsteadOfItself) {
	InMemoryDevice device{makeChainDisk(), kSector};
	const auto found = readMbrPartitions(device);
	ASSERT_TRUE(found.hasValue());
	ASSERT_EQ(found.value().size(), 3U);
	EXPECT_EQ(found.value().at(0).startBytes, byteOffsetOf(1000 + kEbrGap));
	EXPECT_EQ(found.value().at(1).startBytes, byteOffsetOf(1500 + kEbrGap));
	EXPECT_EQ(found.value().at(2).startBytes, byteOffsetOf(1800 + kEbrGap));
}

TEST(MbrPartitions, ChainPartitionsAreReportedAsLogicalWithTheirOwnLengths) {
	InMemoryDevice device{makeChainDisk(), kSector};
	const auto found = readMbrPartitions(device);
	ASSERT_TRUE(found.hasValue());
	ASSERT_EQ(found.value().size(), 3U);
	EXPECT_TRUE(found.value().at(1).logical);
	EXPECT_EQ(found.value().at(1).lengthBytes, byteOffsetOf(200));
	EXPECT_EQ(found.value().at(1).typeCode, kLinuxType);
}

TEST(MbrPartitions, AChainThatRevisitsALinkStopsThere) {
	auto disk = makeDisk();
	writeExtendedEntry(disk);
	writeEbr(disk, EbrLink{.lba = 1000, .sectors = 100, .nextRelative = 500});
	writeEbr(disk, EbrLink{.lba = 1500, .sectors = 200, .nextRelative = 500});
	InMemoryDevice device{disk, kSector};
	const auto found = readMbrPartitions(device);
	ASSERT_TRUE(found.hasValue());
	EXPECT_EQ(found.value().size(), 2U);
}

TEST(MbrPartitions, AChainLeavingTheDeviceKeepsWhatCameBeforeIt) {
	auto disk = makeDisk();
	writeExtendedEntry(disk);
	writeEbr(disk, EbrLink{.lba = 1000, .sectors = 100, .nextRelative = 100000});
	InMemoryDevice device{disk, kSector};
	const auto found = readMbrPartitions(device);
	ASSERT_TRUE(found.hasValue());
	EXPECT_EQ(found.value().size(), 1U);
}

TEST(MbrPartitions, AChainLongerThanTheCapStopsAtIt) {
	auto disk = makeDisk();
	writeSlot(
		disk,
		SlotAt{.lba = 0, .index = 0},
		Slot{.type = kExtendedType, .startLba = kExtendedStart, .sectorCount = 3000});
	for (std::uint32_t link = 0; link < 200; ++link) {
		writeEbr(
			disk,
			EbrLink{
				.lba = kExtendedStart + (2ULL * link),
				.sectors = 1,
				.nextRelative = 2 * (link + 1)});
	}
	InMemoryDevice device{disk, kSector};
	const auto found = readMbrPartitions(device);
	ASSERT_TRUE(found.hasValue());
	EXPECT_EQ(found.value().size(), kMaxLogicalPartitions);
}

// Every byte of a GPT disk belongs to its GPT; handing back the placeholder as
// a partition would be handing back one wrong answer instead of the real ones.
TEST(MbrPartitions, AProtectiveTableIsRefusedAtItsTypeByte) {
	auto disk = makeDisk();
	writeSlot(
		disk,
		SlotAt{.lba = 0, .index = 0},
		Slot{.type = kProtectiveType, .startLba = 1, .sectorCount = kDiskSectors - 1});
	InMemoryDevice device{disk, kSector};
	EXPECT_EQ(revenant::testing::rejectionOf(readMbrPartitions(device)), invalidAt(0x1C2));
}

TEST(MbrPartitions, ASectorZeroThatIsNotATableIsTheParsersRejection) {
	const std::vector<std::byte> disk(kDiskSectors * kSector, std::byte{0});
	InMemoryDevice device{disk, kSector};
	EXPECT_EQ(
		revenant::testing::rejectionOf(readMbrPartitions(device)),
		invalidAt(kSignatureOffset));
}

TEST(MbrPartitions, ADeviceTooSmallToHoldATableIsOutOfRange) {
	const std::vector<std::byte> stub(100, std::byte{0});
	InMemoryDevice device{stub, kSector};
	EXPECT_EQ(revenant::testing::rejectionOf(readMbrPartitions(device)), outOfRangeAt(stub.size()));
}

TEST(MbrPartitions, ADeviceReportingNoSectorSizeIsRejected) {
	InMemoryDevice device{makeTwoPartitionDisk(), 0};
	EXPECT_EQ(revenant::testing::rejectionOf(readMbrPartitions(device)), invalidAt(0));
}

} // namespace
