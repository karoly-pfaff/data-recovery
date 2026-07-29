// SPDX-License-Identifier: GPL-3.0-or-later
// story-0043: the sector-0 partition table. Most of what is asserted here is
// that the table is told apart from a boot sector carrying the same signature
// over unrelated code — the status bytes and the start-of-disk rule are what do
// it — and that an unused slot's stale bytes are never read as a partition.
#include "revenant/volume/Mbr.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::toLittleEndian;
using revenant::testing::invalidAt;
using revenant::testing::outOfRangeAt;
using revenant::testing::Rejection;
using revenant::volume::defersToGpt;
using revenant::volume::kMbrSectorBytes;
using revenant::volume::MbrTable;
using revenant::volume::parseMbrSector;

constexpr std::size_t kTableOffset = 0x1BE;
constexpr std::size_t kEntryBytes = 16;
constexpr std::size_t kSignatureOffset = 0x1FE;
constexpr std::uint16_t kSignature = 0xAA55;

constexpr std::uint8_t kBootableStatus = 0x80;
constexpr std::uint8_t kNtfsType = 0x07;
constexpr std::uint8_t kLinuxType = 0x83;
constexpr std::uint8_t kProtectiveType = 0xEE;

// One slot's four meaningful bytes, named so a fixture reads like the table it
// builds. The CHS triples in between are left zero: nothing reads them.
struct Slot {
	std::uint8_t status = 0x00;
	std::uint8_t type = 0x00;
	std::uint32_t startLba = 0;
	std::uint32_t sectorCount = 0;
};

void writeLe(std::vector<std::byte>& sector, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, sector.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] constexpr std::size_t slotOffset(std::size_t index) {
	return kTableOffset + (index * kEntryBytes);
}

void writeSlot(std::vector<std::byte>& sector, std::size_t index, const Slot& slot) {
	const auto at = slotOffset(index);
	writeLe(sector, at + 0x00, slot.status);
	writeLe(sector, at + 0x04, slot.type);
	writeLe(sector, at + 0x08, slot.startLba);
	writeLe(sector, at + 0x0C, slot.sectorCount);
}

// A signed, otherwise empty sector: four unused slots and nothing else.
[[nodiscard]] std::vector<std::byte> makeEmptyTable() {
	std::vector<std::byte> sector(kMbrSectorBytes, std::byte{0});
	writeLe(sector, kSignatureOffset, kSignature);
	return sector;
}

// The disk both parser tests below start from: a bootable NTFS partition
// followed by a Linux one.
[[nodiscard]] std::vector<std::byte> makeTwoPartitionTable() {
	auto sector = makeEmptyTable();
	writeSlot(
		sector,
		0,
		Slot{.status = kBootableStatus, .type = kNtfsType, .startLba = 2048, .sectorCount = 4096});
	writeSlot(
		sector,
		1,
		Slot{.status = 0x00, .type = kLinuxType, .startLba = 8192, .sectorCount = 512});
	return sector;
}

[[nodiscard]] Rejection rejectionOf(std::span<const std::byte> sector) {
	return revenant::testing::rejectionOf(parseMbrSector(sector));
}

TEST(Mbr, ReadsTheTypeAndExtentOfEveryUsedSlot) {
	const auto parsed = parseMbrSector(makeTwoPartitionTable());
	ASSERT_TRUE(parsed.hasValue());
	const MbrTable& table = parsed.value();
	EXPECT_EQ(table.entries.at(0).type, kNtfsType);
	EXPECT_EQ(table.entries.at(0).startLba, 2048U);
	EXPECT_EQ(table.entries.at(0).sectorCount, 4096U);
	EXPECT_EQ(table.entries.at(1).type, kLinuxType);
	EXPECT_EQ(table.entries.at(1).startLba, 8192U);
	EXPECT_EQ(table.entries.at(1).sectorCount, 512U);
}

TEST(Mbr, LeavesUnusedSlotsUnused) {
	const auto parsed = parseMbrSector(makeTwoPartitionTable());
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_EQ(parsed.value().entries.at(2).type, 0U);
	EXPECT_EQ(parsed.value().entries.at(3).type, 0U);
}

TEST(Mbr, ASpanShorterThanASectorIsOutOfRange) {
	const std::vector<std::byte> stub(100, std::byte{0});
	EXPECT_EQ(rejectionOf(stub), outOfRangeAt(stub.size()));
}

TEST(Mbr, AMissingSignatureIsRejected) {
	auto sector = makeTwoPartitionTable();
	writeLe(sector, kSignatureOffset, static_cast<std::uint16_t>(0U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(kSignatureOffset));
}

// The signature alone is a one-in-65536 coincidence; four status bytes that are
// each 0x00 or 0x80 is what actually tells a table from boot code.
TEST(Mbr, AStatusByteThatIsNeitherZeroNorBootableIsRejected) {
	auto sector = makeTwoPartitionTable();
	writeLe(sector, slotOffset(2), static_cast<std::uint8_t>(0x1FU));
	EXPECT_EQ(rejectionOf(sector), invalidAt(slotOffset(2)));
}

TEST(Mbr, AUsedSlotWithNoSectorsIsRejected) {
	auto sector = makeTwoPartitionTable();
	writeLe(sector, slotOffset(1) + 0x0C, static_cast<std::uint32_t>(0U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(slotOffset(1) + 0x0C));
}

// LBA 0 is the table's own sector: nothing can be partitioned there.
TEST(Mbr, AUsedSlotStartingAtTheTableItselfIsRejected) {
	auto sector = makeTwoPartitionTable();
	writeLe(sector, slotOffset(0) + 0x08, static_cast<std::uint32_t>(0U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(slotOffset(0) + 0x08));
}

// Writers leave stale bytes behind a zeroed type byte; reading them would
// invent partitions out of the previous layout.
TEST(Mbr, AnUnusedSlotIsAcceptedWhateverItsRemainingBytesHold) {
	auto sector = makeTwoPartitionTable();
	writeLe(sector, slotOffset(3) + 0x08, static_cast<std::uint32_t>(0U));
	writeLe(sector, slotOffset(3) + 0x0C, static_cast<std::uint32_t>(0xDEADBEEFU));
	const auto parsed = parseMbrSector(sector);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_EQ(parsed.value().entries.at(3).sectorCount, 0xDEADBEEFU);
}

TEST(Mbr, AnEmptyTableParsesToFourUnusedSlots) {
	const auto parsed = parseMbrSector(makeEmptyTable());
	ASSERT_TRUE(parsed.hasValue());
	const bool allUnused = std::ranges::all_of(parsed.value().entries, [](const auto& entry) {
		return entry.type == 0U && entry.sectorCount == 0U;
	});
	EXPECT_TRUE(allUnused);
}

// --- The question a scheme choice asks of sector 0 ---------------------------

[[nodiscard]] bool deferral(const std::vector<std::byte>& sector) {
	const auto parsed = parseMbrSector(sector);
	return parsed.hasValue() && defersToGpt(parsed.value());
}

[[nodiscard]] std::vector<std::byte> makeProtectiveTable() {
	auto sector = makeEmptyTable();
	writeSlot(sector, 0, Slot{.type = kProtectiveType, .startLba = 1, .sectorCount = 0xFFFFFFFF});
	return sector;
}

TEST(Mbr, AProtectiveTableDefersToTheGpt) {
	EXPECT_TRUE(deferral(makeProtectiveTable()));
}

TEST(Mbr, AnEmptyTableDoesNotDeferToAGpt) {
	EXPECT_FALSE(deferral(makeEmptyTable()));
}

TEST(Mbr, ANormalTableDoesNotDeferToAGpt) {
	EXPECT_FALSE(deferral(makeTwoPartitionTable()));
}

// A hybrid MBR carries the guard entry *and* real ones, so a legacy reader can
// still boot from it. Those entries are a curated subset of what the GPT holds,
// so the GPT is still the complete answer — and reading this table instead would
// hand back a partial one. `readMbrPartitions` refuses it for the same reason.
TEST(Mbr, AHybridTableDefersToTheGptAsWell) {
	auto sector = makeProtectiveTable();
	writeSlot(sector, 1, Slot{.type = kNtfsType, .startLba = 2048, .sectorCount = 4096});
	EXPECT_TRUE(deferral(sector));
}

} // namespace
