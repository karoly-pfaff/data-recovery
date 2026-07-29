// SPDX-License-Identifier: GPL-3.0-or-later
// story-0307: the search that finds an ext4 deletion. Nothing marks a deleted
// entry — its neighbour's record simply grew over it — so this is a hunt through
// bytes that may equally be padding. Half of these tests are about what it must
// *not* find.
#include "fs/ext4/DirectoryHole.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ext4/Ext4Records.hpp"
#include "revenant/fs/NameDecode.hpp"

namespace {

using revenant::fs::decodeRawName;
using revenant::fs::ext4::deletedEntriesIn;
using revenant::fs::ext4::HoleBounds;
using revenant::fs::ext4::HoleEntry;
using revenant::fs::ext4::liveEntryBytes;
using revenant::imagegen::putBytes;
using revenant::imagegen::ext4::dirEntry;
using revenant::imagegen::ext4::DirEntrySpec;

constexpr std::uint32_t kInodeCount = 64;
constexpr std::uint8_t kRegularFileType = 1;

// A live entry whose record grew to swallow `hidden`, exactly as ext4 leaves one
// after a deletion.
struct SwallowSpec {
	std::string_view liveName;
	std::vector<DirEntrySpec> hidden;
};

// The live entry's record grew by the whole of everything it swallowed.
[[nodiscard]] std::uint16_t grownRecordBytes(const SwallowSpec& spec) {
	std::size_t total = liveEntryBytes(spec.liveName.size());
	for (const DirEntrySpec& entry : spec.hidden) {
		total += entry.recordBytes;
	}
	return static_cast<std::uint16_t>(total);
}

[[nodiscard]] std::vector<std::byte> makeRecord(const SwallowSpec& spec) {
	std::vector<std::byte> record = dirEntry(
		DirEntrySpec{
			.inode = 11,
			.name = spec.liveName,
			.fileType = kRegularFileType,
			.recordBytes = grownRecordBytes(spec)});
	std::size_t at = liveEntryBytes(spec.liveName.size());
	for (const DirEntrySpec& entry : spec.hidden) {
		putBytes(record, at, dirEntry(entry));
		at += entry.recordBytes;
	}
	return record;
}

[[nodiscard]] HoleBounds boundsAfter(std::size_t liveBytes) {
	return HoleBounds{.liveBytes = liveBytes, .inodeCount = kInodeCount};
}

[[nodiscard]] std::vector<HoleEntry> holeOf(const SwallowSpec& spec) {
	const auto record = makeRecord(spec);
	return deletedEntriesIn(record, boundsAfter(liveEntryBytes(spec.liveName.size())));
}

[[nodiscard]] DirEntrySpec
hidden(std::uint32_t inode, std::string_view name, std::uint16_t recordBytes) {
	return DirEntrySpec{
		.inode = inode,
		.name = name,
		.fileType = kRegularFileType,
		.recordBytes = recordBytes};
}

[[nodiscard]] std::string nameOf(const HoleEntry& entry) {
	return decodeRawName(entry.nameBytes).utf8;
}

// A live entry occupies its header plus its name, rounded up to the four-byte
// boundary the next record starts on. Everything after that is hole.
TEST(Ext4DirectoryHole, ALiveEntrysOwnBytesAreItsHeaderAndNameRoundedUp) {
	EXPECT_EQ(liveEntryBytes(8), 16U);
	EXPECT_EQ(liveEntryBytes(9), 20U);
	EXPECT_EQ(liveEntryBytes(0), 8U);
}

TEST(Ext4DirectoryHole, ADeletedEntryIsFoundBehindTheOneThatSwallowedIt) {
	const auto found =
		holeOf(SwallowSpec{.liveName = "keep.txt", .hidden = {hidden(14, "gone.txt", 16)}});
	ASSERT_EQ(found.size(), 1U);
	EXPECT_EQ(found.front().inode, 14U);
	EXPECT_EQ(nameOf(found.front()), "gone.txt");
}

// One record can swallow more than one deletion, so the search does not stop at
// the first thing it finds.
TEST(Ext4DirectoryHole, TwoDeletedEntriesInOneHoleAreBothFound) {
	const auto found = holeOf(
		SwallowSpec{
			.liveName = "keep.txt",
			.hidden = {hidden(14, "gone.txt", 16), hidden(15, "later.bin", 20)}});
	ASSERT_EQ(found.size(), 2U);
	EXPECT_EQ(nameOf(found.front()), "gone.txt");
	EXPECT_EQ(nameOf(found.back()), "later.bin");
}

TEST(Ext4DirectoryHole, ALiveEntryWithNoHoleBehindItYieldsNothing) {
	EXPECT_TRUE(holeOf(SwallowSpec{.liveName = "keep.txt", .hidden = {}}).empty());
}

// The common case: the hole is padding a formatter zeroed, and inode 0 is what
// says so.
TEST(Ext4DirectoryHole, AHoleOfZerosYieldsNothing) {
	const std::vector<std::byte> record(64, std::byte{0});
	EXPECT_TRUE(deletedEntriesIn(record, boundsAfter(16)).empty());
}

// Any four aligned bytes can be read as an inode number. A number the volume
// could not have is what keeps the search from inventing a file out of content.
TEST(Ext4DirectoryHole, AnInodeNumberTheVolumeCouldNotHaveIsRejected) {
	const auto found =
		holeOf(SwallowSpec{.liveName = "keep.txt", .hidden = {hidden(9999, "gone.txt", 16)}});
	EXPECT_TRUE(found.empty());
}

// A NUL can never appear in an ext4 name, so bytes carrying one are padding or
// content rather than a name.
TEST(Ext4DirectoryHole, ANameCarryingAByteANameCannotHoldIsRejected) {
	auto record =
		makeRecord(SwallowSpec{.liveName = "keep.txt", .hidden = {hidden(14, "gone.txt", 16)}});
	record.at(16 + 8 + 2) = std::byte{0};
	EXPECT_TRUE(deletedEntriesIn(record, boundsAfter(16)).empty());
}

TEST(Ext4DirectoryHole, AnEntryWithNoNameAtAllIsRejected) {
	const auto found = holeOf(SwallowSpec{.liveName = "keep.txt", .hidden = {hidden(14, "", 8)}});
	EXPECT_TRUE(found.empty());
}

} // namespace
