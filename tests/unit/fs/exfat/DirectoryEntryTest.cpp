// SPDX-License-Identifier: GPL-3.0-or-later
// story-0032: one exFAT directory slot. The point of most of these is that
// deleting a set clears one bit of each type byte and touches nothing else —
// which is why exFAT gives a deleted file its whole name back, and FAT does not.
#include "revenant/fs/exfat/DirectoryEntry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "revenant/core/Error.hpp"

namespace {

using revenant::ErrorCode;
using revenant::toLittleEndian;
using revenant::fs::exfat::classifyExfatEntry;
using revenant::fs::exfat::ExfatEntryKind;
using revenant::fs::exfat::kDirectoryEntryBytes;
using revenant::fs::exfat::parseFileEntry;
using revenant::fs::exfat::parseFileName;
using revenant::fs::exfat::parseStreamExtension;

constexpr std::uint8_t kFileType = 0x85;
constexpr std::uint8_t kStreamType = 0xC0;
constexpr std::uint8_t kNameType = 0xC1;
constexpr std::uint8_t kBitmapType = 0x81;
constexpr std::uint8_t kLabelType = 0x83;
constexpr std::uint8_t kInUseBit = 0x80;

constexpr std::uint32_t kFirstCluster = 0x0001'2345;
constexpr std::uint64_t kDataLength = 9000;

void writeLe(std::vector<std::byte>& slot, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, slot.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] std::vector<std::byte> makeSlot(std::uint8_t type) {
	std::vector<std::byte> slot(kDirectoryEntryBytes, std::byte{0});
	writeLe(slot, 0x00, type);
	return slot;
}

[[nodiscard]] std::vector<std::byte> makeStreamSlot(std::uint8_t flags) {
	auto slot = makeSlot(kStreamType);
	writeLe(slot, 0x01, flags);
	writeLe(slot, 0x03, static_cast<std::uint8_t>(14U));
	writeLe(slot, 0x08, kDataLength);
	writeLe(slot, 0x14, kFirstCluster);
	writeLe(slot, 0x18, kDataLength);
	return slot;
}

[[nodiscard]] ExfatEntryKind kindOf(const std::vector<std::byte>& slot) {
	const auto header = classifyExfatEntry(slot);
	EXPECT_TRUE(header.hasValue());
	return header.hasValue() ? header.value().kind : ExfatEntryKind::kUnknown;
}

TEST(ExfatDirectoryEntry, AZeroTypeByteEndsTheDirectory) {
	EXPECT_EQ(kindOf(makeSlot(0)), ExfatEntryKind::kEndOfDirectory);
}

TEST(ExfatDirectoryEntry, NamesEachTypeItReads) {
	EXPECT_EQ(kindOf(makeSlot(kFileType)), ExfatEntryKind::kFile);
	EXPECT_EQ(kindOf(makeSlot(kStreamType)), ExfatEntryKind::kStreamExtension);
	EXPECT_EQ(kindOf(makeSlot(kNameType)), ExfatEntryKind::kFileName);
	EXPECT_EQ(kindOf(makeSlot(kBitmapType)), ExfatEntryKind::kAllocationBitmap);
	EXPECT_EQ(kindOf(makeSlot(kLabelType)), ExfatEntryKind::kVolumeLabel);
}

// A type this build does not read is skipped, never guessed at.
TEST(ExfatDirectoryEntry, AnUnrecognizedTypeIsSaidToBeUnknown) {
	EXPECT_EQ(kindOf(makeSlot(0xA7)), ExfatEntryKind::kUnknown);
}

// Deleting a set clears bit 7 of every type byte in it — and nothing else. The
// slot is still a file entry; it just is not in use.
TEST(ExfatDirectoryEntry, ADeletedSetKeepsItsTypeAndLosesOnlyTheInUseBit) {
	const auto slot = makeSlot(kFileType & ~kInUseBit);
	const auto header = classifyExfatEntry(slot);
	ASSERT_TRUE(header.hasValue());
	EXPECT_EQ(header.value().kind, ExfatEntryKind::kFile);
	EXPECT_FALSE(header.value().inUse);
}

TEST(ExfatDirectoryEntry, AnInUseSetSaysSo) {
	const auto header = classifyExfatEntry(makeSlot(kFileType));
	ASSERT_TRUE(header.hasValue());
	EXPECT_TRUE(header.value().inUse);
}

TEST(ExfatDirectoryEntry, AShortSlotIsOutOfRange) {
	const std::vector<std::byte> stub(16, std::byte{0});
	const auto header = classifyExfatEntry(stub);
	ASSERT_FALSE(header.hasValue());
	EXPECT_EQ(header.error().code, ErrorCode::kOutOfRange);
}

TEST(ExfatDirectoryEntry, ReadsAFileEntrysSecondaryCountAndDirectoryBit) {
	auto slot = makeSlot(kFileType);
	writeLe(slot, 0x01, static_cast<std::uint8_t>(3U));
	writeLe(slot, 0x04, static_cast<std::uint16_t>(0x0010U));
	const auto entry = parseFileEntry(slot);
	ASSERT_TRUE(entry.hasValue());
	EXPECT_EQ(entry.value().secondaryCount, 3U);
	EXPECT_TRUE(entry.value().isDirectory);
}

TEST(ExfatDirectoryEntry, ReadsAStreamExtensionsClusterLengthAndNameLength) {
	const auto entry = parseStreamExtension(makeStreamSlot(0x01));
	ASSERT_TRUE(entry.hasValue());
	EXPECT_EQ(entry.value().firstCluster, kFirstCluster);
	EXPECT_EQ(entry.value().dataLength, kDataLength);
	EXPECT_EQ(entry.value().validDataLength, kDataLength);
	EXPECT_EQ(entry.value().nameLength, 14U);
	EXPECT_FALSE(entry.value().noFatChain);
}

// A contiguous file says so and skips the FAT. For a deleted file that is the
// difference between a stated extent and a guess.
TEST(ExfatDirectoryEntry, AContiguousFileSaysItsChainIsNotInTheFat) {
	const auto entry = parseStreamExtension(makeStreamSlot(0x03));
	ASSERT_TRUE(entry.hasValue());
	EXPECT_TRUE(entry.value().noFatChain);
}

TEST(ExfatDirectoryEntry, AFileNameFragmentYieldsItsFifteenCodeUnits) {
	auto slot = makeSlot(kNameType);
	writeLe(slot, 0x02, static_cast<std::uint16_t>(u'k'));
	const auto fragment = parseFileName(slot);
	ASSERT_TRUE(fragment.hasValue());
	EXPECT_EQ(fragment.value().size(), 30U);
	EXPECT_EQ(fragment.value().front(), std::byte{'k'});
}

} // namespace
