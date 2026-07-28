// SPDX-License-Identifier: GPL-3.0-or-later
// story-0030: one 32-byte directory slot, classified and read. A deleted file
// is not a kind of its own here — the marker takes a name byte and leaves
// everything else standing, which is precisely what makes FAT undelete
// possible at all.
#include "revenant/fs/fat/DirectoryEntry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "revenant/core/Error.hpp"

namespace {

using revenant::ErrorCode;
using revenant::toLittleEndian;
using revenant::fs::fat::classifyEntry;
using revenant::fs::fat::EntryKind;
using revenant::fs::fat::kDirectoryEntryBytes;
using revenant::fs::fat::parseLongNameFragment;
using revenant::fs::fat::parseShortEntry;

constexpr std::uint8_t kAttrArchive = 0x20;
constexpr std::uint8_t kAttrDirectory = 0x10;
constexpr std::uint8_t kAttrVolumeLabel = 0x08;
constexpr std::uint8_t kAttrLongName = 0x0F;

constexpr std::uint32_t kFirstCluster = 0x0001'2345;
constexpr std::uint32_t kFileSize = 9000;

// 1980-01-01, the FAT epoch, and noon on it — the same two values
// tests/unit/fs/fat/DosTimeTest.cpp derives its expectations from.
constexpr std::uint16_t kEpochDate = (0U << 9U) | (1U << 5U) | 1U;
constexpr std::uint16_t kNoonTime = 12U << 11U;
constexpr std::uint64_t kEpochTicks = 119'600'064'000'000'000ULL;
constexpr std::uint64_t kNoonTicks = 12ULL * 3600ULL * 10'000'000ULL;

void writeLe(std::vector<std::byte>& slot, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, slot.begin() + static_cast<std::ptrdiff_t>(offset));
}

void writeName(std::vector<std::byte>& slot, std::string_view eleven) {
	std::ranges::transform(eleven, slot.begin(), [](char c) { return static_cast<std::byte>(c); });
}

// A file entry: `KEEP.JPG`, 9000 bytes, starting at cluster 0x12345.
[[nodiscard]] std::vector<std::byte> makeFileSlot() {
	std::vector<std::byte> slot(kDirectoryEntryBytes, std::byte{0});
	writeName(slot, "KEEP    JPG");
	writeLe(slot, 0x0B, kAttrArchive);
	writeLe(slot, 0x14, static_cast<std::uint16_t>(kFirstCluster >> 16U));
	writeLe(slot, 0x1A, static_cast<std::uint16_t>(kFirstCluster & 0xFFFFU));
	writeLe(slot, 0x1C, kFileSize);
	return slot;
}

// A long-name fragment holding "ab", the last of its name, checksum 0x5A.
[[nodiscard]] std::vector<std::byte> makeFragmentSlot() {
	std::vector<std::byte> slot(kDirectoryEntryBytes, std::byte{0});
	writeLe(slot, 0x00, static_cast<std::uint8_t>(0x41U));
	writeLe(slot, 0x01, static_cast<std::uint16_t>(u'a'));
	writeLe(slot, 0x03, static_cast<std::uint16_t>(u'b'));
	writeLe(slot, 0x0B, kAttrLongName);
	writeLe(slot, 0x0D, static_cast<std::uint8_t>(0x5AU));
	return slot;
}

[[nodiscard]] EntryKind kindOf(const std::vector<std::byte>& slot) {
	const auto kind = classifyEntry(slot);
	EXPECT_TRUE(kind.hasValue());
	return kind.hasValue() ? kind.value() : EntryKind::kEndOfDirectory;
}

[[nodiscard]] std::vector<std::byte> withAttribute(std::uint8_t attribute) {
	auto slot = makeFileSlot();
	writeLe(slot, 0x0B, attribute);
	return slot;
}

TEST(FatDirectoryEntry, AZeroFirstByteEndsTheDirectory) {
	const std::vector<std::byte> empty(kDirectoryEntryBytes, std::byte{0});
	EXPECT_EQ(kindOf(empty), EntryKind::kEndOfDirectory);
}

TEST(FatDirectoryEntry, AnOrdinaryEntryIsAFile) {
	EXPECT_EQ(kindOf(makeFileSlot()), EntryKind::kFile);
}

TEST(FatDirectoryEntry, TheDirectoryBitMakesItADirectory) {
	EXPECT_EQ(kindOf(withAttribute(kAttrDirectory)), EntryKind::kDirectory);
}

TEST(FatDirectoryEntry, TheVolumeLabelBitNamesNoFile) {
	EXPECT_EQ(kindOf(withAttribute(kAttrVolumeLabel)), EntryKind::kVolumeLabel);
}

TEST(FatDirectoryEntry, TheLongNameAttributeOutranksTheVolumeLabelBitInsideIt) {
	EXPECT_EQ(kindOf(withAttribute(kAttrLongName)), EntryKind::kLongName);
}

// A deleted fragment still holds its characters, so it must still classify as
// a fragment however its first byte was overwritten.
TEST(FatDirectoryEntry, ADeletedFragmentIsStillAFragment) {
	auto slot = makeFragmentSlot();
	writeLe(slot, 0x00, static_cast<std::uint8_t>(0xE5U));
	EXPECT_EQ(kindOf(slot), EntryKind::kLongName);
}

TEST(FatDirectoryEntry, AShortSlotIsOutOfRange) {
	const std::vector<std::byte> stub(16, std::byte{0});
	const auto kind = classifyEntry(stub);
	ASSERT_FALSE(kind.hasValue());
	EXPECT_EQ(kind.error().code, ErrorCode::kOutOfRange);
}

// FAT stores a date but no time for the last access, so the stamp lands at
// midnight — the format's precision, not a field the parser dropped, and not
// the access *date* misread as a time of day.
TEST(FatDirectoryEntry, TheAccessStampIsMidnightOnItsDate) {
	auto slot = makeFileSlot();
	writeLe(slot, 0x0E, kNoonTime);
	writeLe(slot, 0x10, kEpochDate);
	writeLe(slot, 0x12, kEpochDate);
	const auto entry = parseShortEntry(slot);
	ASSERT_TRUE(entry.hasValue());
	EXPECT_EQ(entry.value().timestamps.created, kEpochTicks + kNoonTicks);
	EXPECT_EQ(entry.value().timestamps.accessed, kEpochTicks);
}

TEST(FatDirectoryEntry, ReadsANamedFileWithItsClusterAndSize) {
	const auto entry = parseShortEntry(makeFileSlot());
	ASSERT_TRUE(entry.hasValue());
	EXPECT_EQ(entry.value().name.utf8, "KEEP.JPG");
	EXPECT_EQ(entry.value().firstCluster, kFirstCluster);
	EXPECT_EQ(entry.value().sizeInBytes, kFileSize);
	EXPECT_FALSE(entry.value().deleted);
}

// Everything but the name survives a deletion. That is the whole premise.
TEST(FatDirectoryEntry, ADeletedFileKeepsItsClusterAndSize) {
	auto slot = makeFileSlot();
	writeLe(slot, 0x00, static_cast<std::uint8_t>(0xE5U));
	const auto entry = parseShortEntry(slot);
	ASSERT_TRUE(entry.hasValue());
	EXPECT_TRUE(entry.value().deleted);
	EXPECT_EQ(entry.value().name.utf8, "_EEP.JPG");
	EXPECT_EQ(entry.value().firstCluster, kFirstCluster);
	EXPECT_EQ(entry.value().sizeInBytes, kFileSize);
}

TEST(FatDirectoryEntry, ReadsAFragmentsOrdinalFlagChecksumAndCharacters) {
	const auto fragment = parseLongNameFragment(makeFragmentSlot());
	ASSERT_TRUE(fragment.hasValue());
	EXPECT_EQ(fragment.value().ordinal, 1U);
	EXPECT_TRUE(fragment.value().last);
	EXPECT_FALSE(fragment.value().deleted);
	EXPECT_EQ(fragment.value().checksum, 0x5AU);
	EXPECT_EQ(fragment.value().nameBytes.size(), 4U);
}

// The marker overwrites the byte the ordinal and the last-fragment flag share,
// so a deleted fragment keeps its characters and loses its place.
TEST(FatDirectoryEntry, ADeletedFragmentKeepsItsCharactersAndLosesItsPlace) {
	auto slot = makeFragmentSlot();
	writeLe(slot, 0x00, static_cast<std::uint8_t>(0xE5U));
	const auto fragment = parseLongNameFragment(slot);
	ASSERT_TRUE(fragment.hasValue());
	EXPECT_TRUE(fragment.value().deleted);
	EXPECT_EQ(fragment.value().nameBytes.size(), 4U);
}

TEST(FatDirectoryEntry, AFragmentWithNoPlaceInTheNameIsRejected) {
	auto slot = makeFragmentSlot();
	writeLe(slot, 0x00, static_cast<std::uint8_t>(0x00U));
	const auto fragment = parseLongNameFragment(slot);
	ASSERT_FALSE(fragment.hasValue());
	EXPECT_EQ(fragment.error().code, ErrorCode::kInvalidArgument);
}

// A name shorter than its fragment ends at a NUL; the 0xFFFF padding after it
// is layout, not characters.
TEST(FatDirectoryEntry, AFragmentStopsAtTheNulThatEndsAShortName) {
	auto slot = makeFragmentSlot();
	writeLe(slot, 0x03, static_cast<std::uint16_t>(0U));
	writeLe(slot, 0x05, static_cast<std::uint16_t>(0xFFFFU));
	const auto fragment = parseLongNameFragment(slot);
	ASSERT_TRUE(fragment.hasValue());
	EXPECT_EQ(fragment.value().nameBytes.size(), 2U);
}

} // namespace
