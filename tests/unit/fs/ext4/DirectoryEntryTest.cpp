// SPDX-License-Identifier: GPL-3.0-or-later
// story-0306: one linear ext4 directory entry. `rec_len` is the whole story: it
// is the distance to the *next* entry, and a deletion works by growing the
// previous entry's until it swallows this one — so a record that does not point
// at a real entry has to end the walk rather than send it somewhere arbitrary.
#include "revenant/fs/ext4/DirectoryEntry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "revenant/fs/NameDecode.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::toLittleEndian;
using revenant::fs::decodeRawName;
using revenant::fs::ext4::Ext4DirEntry;
using revenant::fs::ext4::Ext4FileType;
using revenant::fs::ext4::kDirEntryHeaderBytes;
using revenant::fs::ext4::parseExt4DirEntry;
using revenant::testing::invalidAt;
using revenant::testing::outOfRangeAt;
using revenant::testing::Rejection;

constexpr std::uint32_t kInode = 12;

// Writes through the checked accessor rather than an iterator: `makeEntry` sizes
// its buffer so every write below is in bounds, and this is what makes a mistake
// in that reasoning throw where it was made instead of corrupting the buffer the
// parser is about to be handed.
void writeLe(std::vector<std::byte>& bytes, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	for (std::size_t index = 0; index < raw.size(); ++index) {
		bytes.at(offset + index) = raw.at(index);
	}
}

void writeName(std::vector<std::byte>& bytes, std::string_view name) {
	for (std::size_t index = 0; index < name.size(); ++index) {
		bytes.at(kDirEntryHeaderBytes + index) = static_cast<std::byte>(name.at(index));
	}
}

// One entry padded out to `recordBytes`, as it lies in a directory block.
struct EntrySpec {
	std::string_view name;
	std::uint8_t type;
	std::uint16_t recordBytes;
};

// The buffer holds everything written into it — the fixed header and the name —
// rather than however many bytes the spec's *length field* claims. The two are
// not the same question: `recordBytes` is a value the parser reads, and it may
// legitimately disagree with what follows it, which is exactly what the
// rejection tests below hand it. Sizing the buffer from that field alone let a
// spec naming fewer bytes than its own name needs write past the end.
[[nodiscard]] std::vector<std::byte> makeEntry(const EntrySpec& spec) {
	const auto written = kDirEntryHeaderBytes + spec.name.size();
	std::vector<std::byte> bytes(std::max<std::size_t>(spec.recordBytes, written), std::byte{0});
	writeLe(bytes, 0x00, kInode);
	writeLe(bytes, 0x04, spec.recordBytes);
	writeLe(bytes, 0x06, static_cast<std::uint8_t>(spec.name.size()));
	writeLe(bytes, 0x07, spec.type);
	writeName(bytes, spec.name);
	return bytes;
}

[[nodiscard]] Ext4DirEntry entryOf(const std::vector<std::byte>& bytes) {
	auto parsed = parseExt4DirEntry(bytes);
	EXPECT_TRUE(parsed.hasValue());
	return parsed.hasValue() ? parsed.value() : Ext4DirEntry{};
}

[[nodiscard]] Rejection rejectionOf(const std::vector<std::byte>& bytes) {
	return revenant::testing::rejectionOf(parseExt4DirEntry(bytes));
}

TEST(Ext4DirectoryEntry, AFileEntryReadsBackItsFields) {
	const auto entry =
		entryOf(makeEntry(EntrySpec{.name = "photo.jpg", .type = 1, .recordBytes = 20}));
	EXPECT_EQ(entry.inode, kInode);
	EXPECT_EQ(entry.recordBytes, 20U);
	EXPECT_EQ(entry.type, Ext4FileType::kRegularFile);
}

// The bytes are handed on undecoded: ext4 enforces no encoding, so what they
// mean is the name decoder's question, not this parser's.
TEST(Ext4DirectoryEntry, TheNameIsHandedOnAsRawBytes) {
	const auto entry =
		entryOf(makeEntry(EntrySpec{.name = "photo.jpg", .type = 1, .recordBytes = 20}));
	ASSERT_EQ(entry.nameBytes.size(), 9U);
	EXPECT_EQ(decodeRawName(entry.nameBytes).utf8, "photo.jpg");
}

TEST(Ext4DirectoryEntry, ADirectoryEntryIsNamedAsOne) {
	const auto entry =
		entryOf(makeEntry(EntrySpec{.name = "photos", .type = 2, .recordBytes = 16}));
	EXPECT_EQ(entry.type, Ext4FileType::kDirectory);
}

TEST(Ext4DirectoryEntry, ATypeThisBuildDoesNotActOnIsNamedOther) {
	const auto entry = entryOf(makeEntry(EntrySpec{.name = "link", .type = 7, .recordBytes = 12}));
	EXPECT_EQ(entry.type, Ext4FileType::kOther);
}

TEST(Ext4DirectoryEntry, AnEntryWithNoStatedTypeIsUnknown) {
	const auto entry = entryOf(makeEntry(EntrySpec{.name = "x", .type = 0, .recordBytes = 12}));
	EXPECT_EQ(entry.type, Ext4FileType::kUnknown);
}

// Every directory begins with these two, and both are entries like any other.
TEST(Ext4DirectoryEntry, TheDotEntriesParseLikeAnyOther) {
	EXPECT_EQ(
		entryOf(makeEntry(EntrySpec{.name = ".", .type = 2, .recordBytes = 12})).recordBytes,
		12U);
	EXPECT_EQ(
		entryOf(makeEntry(EntrySpec{.name = "..", .type = 2, .recordBytes = 12})).nameBytes.size(),
		2U);
}

// The record is padding as far as the entry is concerned: a deleted entry
// swallowed by its neighbour leaves a long record with a short name in it.
TEST(Ext4DirectoryEntry, ARecordLongerThanItsNameIsFine) {
	const auto entry = entryOf(makeEntry(EntrySpec{.name = "a", .type = 1, .recordBytes = 4096}));
	EXPECT_EQ(entry.recordBytes, 4096U);
	EXPECT_EQ(entry.nameBytes.size(), 1U);
}

TEST(Ext4DirectoryEntry, ARecordShorterThanTheFixedPartIsRejected) {
	auto bytes = makeEntry(EntrySpec{.name = "a", .type = 1, .recordBytes = 12});
	writeLe(bytes, 0x04, std::uint16_t{4});
	EXPECT_EQ(rejectionOf(bytes), invalidAt(0x04));
}

// The same record stated directly, rather than patched in afterwards — which is
// the case the fixture could not build until it stopped sizing its buffer from
// the length field. Four bytes cannot hold the fixed part, let alone the name;
// the buffer holds both anyway, and the field still says four.
TEST(Ext4DirectoryEntry, ASpecCanStateARecordShorterThanWhatFollowsIt) {
	const auto bytes = makeEntry(EntrySpec{.name = "a", .type = 1, .recordBytes = 4});
	EXPECT_EQ(bytes.size(), kDirEntryHeaderBytes + 1);
	EXPECT_EQ(rejectionOf(bytes), invalidAt(0x04));
}

// Records are four-byte aligned so the next entry's inode number never
// straddles a word; one that is not aligned does not point at an entry.
TEST(Ext4DirectoryEntry, AnUnalignedRecordLengthIsRejected) {
	auto bytes = makeEntry(EntrySpec{.name = "a", .type = 1, .recordBytes = 12});
	writeLe(bytes, 0x04, std::uint16_t{10});
	EXPECT_EQ(rejectionOf(bytes), invalidAt(0x04));
}

TEST(Ext4DirectoryEntry, ARecordReachingPastTheInputIsRejected) {
	auto bytes = makeEntry(EntrySpec{.name = "a", .type = 1, .recordBytes = 12});
	writeLe(bytes, 0x04, std::uint16_t{16});
	EXPECT_EQ(rejectionOf(bytes), invalidAt(0x04));
}

TEST(Ext4DirectoryEntry, ANameRunningPastItsOwnRecordIsRejected) {
	auto bytes = makeEntry(EntrySpec{.name = "a", .type = 1, .recordBytes = 12});
	writeLe(bytes, 0x06, std::uint8_t{200});
	EXPECT_EQ(rejectionOf(bytes), invalidAt(0x06));
}

TEST(Ext4DirectoryEntry, AShortInputIsOutOfRange) {
	const std::vector<std::byte> bytes(kDirEntryHeaderBytes - 1, std::byte{0});
	EXPECT_EQ(rejectionOf(bytes), outOfRangeAt(bytes.size()));
}

} // namespace
