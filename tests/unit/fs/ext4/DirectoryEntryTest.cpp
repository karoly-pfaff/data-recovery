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

// The bound is checked rather than assumed: nothing in the signature stops a
// caller naming a `recordBytes` too small for the header it then writes, and at
// `-O2` GCC says so — it cannot prove the destination is non-empty, because for
// a short enough entry it would not be. A spec that does not fit fails its test
// instead of writing past the vector.
void writeLe(std::vector<std::byte>& bytes, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	ASSERT_GE(bytes.size(), offset + raw.size()) << "field at offset " << offset << " does not fit";
	std::ranges::copy(raw, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

// One entry padded out to `recordBytes`, as it lies in a directory block.
struct EntrySpec {
	std::string_view name;
	std::uint8_t type;
	std::uint16_t recordBytes;
};

[[nodiscard]] std::vector<std::byte> makeEntry(const EntrySpec& spec) {
	std::vector<std::byte> bytes(spec.recordBytes, std::byte{0});
	writeLe(bytes, 0x00, kInode);
	writeLe(bytes, 0x04, spec.recordBytes);
	writeLe(bytes, 0x06, static_cast<std::uint8_t>(spec.name.size()));
	writeLe(bytes, 0x07, spec.type);
	std::ranges::transform(spec.name, bytes.begin() + kDirEntryHeaderBytes, [](char letter) {
		return static_cast<std::byte>(letter);
	});
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
