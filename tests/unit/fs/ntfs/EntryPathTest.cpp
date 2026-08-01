// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ntfs/EntryPath.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "imagegen/ntfs/AttributeBuilder.hpp"
#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/MftRecordBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/core/Utf16Name.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"
#include "support/NtfsVolume.hpp"

namespace {

using revenant::DecodedName;
using revenant::fs::ntfs::EntryPath;
using revenant::fs::ntfs::kMaxPathDepth;
using revenant::fs::ntfs::MftFileName;
using revenant::fs::ntfs::MftTable;
using revenant::fs::ntfs::preferredName;
using revenant::fs::ntfs::resolveEntryPath;
using revenant::imagegen::ntfs::buildEndMarker;
using revenant::imagegen::ntfs::buildFileName;
using revenant::imagegen::ntfs::buildMftRecord;
using revenant::imagegen::ntfs::buildResidentData;
using revenant::imagegen::ntfs::buildStandardInformation;
using revenant::imagegen::ntfs::FileNameSpec;
using revenant::imagegen::ntfs::kDeletedNotesRecord;
using revenant::imagegen::ntfs::kKeepJpegRecord;
using revenant::imagegen::ntfs::kOrphanJpegRecord;
using revenant::imagegen::ntfs::kPhotosRecord;
using revenant::imagegen::ntfs::kRootRecord;
using revenant::imagegen::ntfs::makeLayout;
using revenant::imagegen::ntfs::MftRecordSpec;
using revenant::testing::NtfsVolume;

constexpr std::uint8_t kWin32NameSpace = 1;
constexpr std::uint8_t kDosNameSpace = 2;

[[nodiscard]] std::vector<std::byte> concat(std::span<const std::vector<std::byte>> parts) {
	std::vector<std::byte> out;
	for (const auto& part : parts) {
		out.insert(out.end(), part.begin(), part.end());
	}
	return out;
}

// What a replacement directory record needs to say for a path walk to accept
// or reject it: who it claims to be, and whether it is a directory at all.
struct DirectorySpec {
	std::string_view name;
	std::uint64_t parent;
	std::uint16_t sequence;
	bool isDirectory;
};

[[nodiscard]] std::vector<std::byte> directoryRecord(const DirectorySpec& spec) {
	const std::vector<std::vector<std::byte>> parts{
		buildStandardInformation(),
		buildFileName(
			FileNameSpec{
				.parentRecord = spec.parent,
				.parentSequence = 1,
				.name = spec.name,
				.realSize = 0}),
		buildEndMarker()};
	const auto attributes = concat(parts);
	return buildMftRecord(
		makeLayout(),
		MftRecordSpec{
			.sequence = spec.sequence,
			.inUse = true,
			.isDirectory = spec.isDirectory,
			.attributes = attributes});
}

// A record carrying data but no `$FILE_NAME` at all — metadata that survived
// without the one attribute a path could be built from.
[[nodiscard]] std::vector<std::byte> namelessRecord() {
	const std::vector<std::vector<std::byte>> parts{
		buildStandardInformation(),
		buildResidentData(std::vector<std::byte>(8, std::byte{0x7A})),
		buildEndMarker()};
	const auto attributes = concat(parts);
	return buildMftRecord(
		makeLayout(),
		MftRecordSpec{
			.sequence = 1,
			.inUse = false,
			.isDirectory = false,
			.attributes = attributes});
}

[[nodiscard]] EntryPath pathOf(const MftTable& table, std::uint64_t number) {
	const auto parsed = table.readRecord(number);
	if (!parsed.hasValue()) {
		return EntryPath{.path = "<unreadable>", .reachedRoot = false};
	}
	return resolveEntryPath(table, parsed.value());
}

[[nodiscard]] MftFileName named(std::string_view text, std::uint8_t nameSpace) {
	MftFileName name;
	name.nameSpace = nameSpace;
	name.name = DecodedName{.utf8 = std::string{text}, .lossless = true};
	return name;
}

TEST(NtfsEntryPath, ResolvesARootLevelFileToItsBareName) {
	NtfsVolume volume;
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	const auto path = pathOf(table.value(), kDeletedNotesRecord);
	EXPECT_EQ(path.path, "notes.txt");
	EXPECT_TRUE(path.reachedRoot);
}

TEST(NtfsEntryPath, ResolvesANestedFileThroughItsDirectory) {
	NtfsVolume volume;
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	const auto path = pathOf(table.value(), kKeepJpegRecord);
	EXPECT_EQ(path.path, "photos/keep.jpg");
	EXPECT_TRUE(path.reachedRoot);
}

// The orphan's parent reference points at a record slot nothing lives in.
TEST(NtfsEntryPath, AMissingParentLeavesTheEntryUnrooted) {
	NtfsVolume volume;
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	const auto path = pathOf(table.value(), kOrphanJpegRecord);
	EXPECT_EQ(path.path, "orphan.jpg");
	EXPECT_FALSE(path.reachedRoot);
}

// The slot is still a directory, but it has been reused for a different one:
// the sequence number moved on, so the child's reference is stale.
TEST(NtfsEntryPath, AReusedParentSlotLeavesTheEntryUnrooted) {
	NtfsVolume volume;
	volume.putRecord(
		kPhotosRecord,
		directoryRecord(
			DirectorySpec{
				.name = "photos",
				.parent = kRootRecord,
				.sequence = 2,
				.isDirectory = true}));
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	const auto path = pathOf(table.value(), kKeepJpegRecord);
	EXPECT_EQ(path.path, "keep.jpg");
	EXPECT_FALSE(path.reachedRoot);
}

TEST(NtfsEntryPath, AParentThatIsNotADirectoryLeavesTheEntryUnrooted) {
	NtfsVolume volume;
	volume.putRecord(
		kPhotosRecord,
		directoryRecord(
			DirectorySpec{
				.name = "photos",
				.parent = kRootRecord,
				.sequence = 1,
				.isDirectory = false}));
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	EXPECT_FALSE(pathOf(table.value(), kKeepJpegRecord).reachedRoot);
}

// A directory that is its own parent never reaches the root. The walk must stop
// at the depth bound rather than run forever on a crafted volume.
TEST(NtfsEntryPath, AParentCycleStopsAtTheDepthBound) {
	NtfsVolume volume;
	volume.putRecord(
		kPhotosRecord,
		directoryRecord(
			DirectorySpec{
				.name = "photos",
				.parent = kPhotosRecord,
				.sequence = 1,
				.isDirectory = true}));
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	const auto path = pathOf(table.value(), kKeepJpegRecord);
	EXPECT_FALSE(path.reachedRoot);
	EXPECT_EQ(std::ranges::count(path.path, '/'), static_cast<std::ptrdiff_t>(kMaxPathDepth));
}

TEST(NtfsEntryPath, ARecordWithNoNameHasNoPathAtAll) {
	NtfsVolume volume;
	volume.putRecord(kKeepJpegRecord, namelessRecord());
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	const auto path = pathOf(table.value(), kKeepJpegRecord);
	EXPECT_EQ(path.path, "");
	EXPECT_FALSE(path.reachedRoot);
}

TEST(NtfsPreferredName, IsNothingWhenTheRecordCarriesNoName) {
	EXPECT_EQ(preferredName({}), nullptr);
}

TEST(NtfsPreferredName, IsTheOnlyNameWhenThereIsOne) {
	const std::vector<MftFileName> names{named("holiday.jpg", kWin32NameSpace)};
	ASSERT_NE(preferredName(names), nullptr);
	EXPECT_EQ(preferredName(names)->name.utf8, "holiday.jpg");
}

// The 8.3 alias is a compatibility shadow of the same file; the long name is
// what the user called it.
TEST(NtfsPreferredName, PrefersTheLongNameOverTheDosAliasWhateverTheOrder) {
	const std::vector<MftFileName> names{
		named("HOLIDA~1.JPG", kDosNameSpace),
		named("holiday photo.jpg", kWin32NameSpace)};
	ASSERT_NE(preferredName(names), nullptr);
	EXPECT_EQ(preferredName(names)->name.utf8, "holiday photo.jpg");
}

TEST(NtfsPreferredName, FallsBackToTheDosAliasWhenItIsAllThereIs) {
	const std::vector<MftFileName> names{named("HOLIDA~1.JPG", kDosNameSpace)};
	ASSERT_NE(preferredName(names), nullptr);
	EXPECT_EQ(preferredName(names)->name.utf8, "HOLIDA~1.JPG");
}

} // namespace
