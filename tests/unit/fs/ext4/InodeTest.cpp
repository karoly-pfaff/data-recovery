// SPDX-License-Identifier: GPL-3.0-or-later
// story-0034: one ext4 inode. The three that matter for undelete are that a
// freed inode keeps everything but its link count, that `i_size_high` is only a
// size for a regular file, and that a creation time is reported only when the
// inode actually holds one.
#include "revenant/fs/ext4/Inode.hpp"

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
using revenant::fs::ext4::Ext4Inode;
using revenant::fs::ext4::kMinInodeBytes;
using revenant::fs::ext4::parseExt4Inode;

constexpr std::size_t kLargeInodeBytes = 256;
constexpr std::uint16_t kRegularFileMode = 0x81A4; // -rw-r--r--
constexpr std::uint16_t kDirectoryMode = 0x41ED;   // drwxr-xr-x
constexpr std::uint32_t kExtentsFlag = 0x0008'0000;
constexpr std::uint32_t kSizeLow = 9000;

// 2020-08-01 12:00:00 UTC, and its FILETIME ticks.
constexpr std::uint32_t kModifySeconds = 1'596'283'200;
constexpr std::uint64_t kModifyTicks = 132'407'568'000'000'000ULL;

void writeLe(std::vector<std::byte>& slot, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, slot.begin() + static_cast<std::ptrdiff_t>(offset));
}

// The extra area past the fixed 128 bytes, and the creation time only an inode
// long enough to declare it actually holds.
void writeExtraArea(std::vector<std::byte>& slot) {
	writeLe(slot, 0x80, std::uint16_t{32});
	writeLe(slot, 0x90, kModifySeconds);
}

// A live regular file in a 256-byte inode with an extent tree, which is what
// mke2fs writes today.
[[nodiscard]] std::vector<std::byte> makeInode(std::uint16_t mode) {
	std::vector<std::byte> slot(kLargeInodeBytes, std::byte{0});
	writeLe(slot, 0x00, mode);
	writeLe(slot, 0x04, kSizeLow);
	writeLe(slot, 0x08, kModifySeconds);
	writeLe(slot, 0x10, kModifySeconds);
	writeLe(slot, 0x1A, std::uint16_t{1});
	writeLe(slot, 0x20, kExtentsFlag);
	writeLe(slot, 0x28, std::uint16_t{0xF30A});
	writeExtraArea(slot);
	return slot;
}

[[nodiscard]] Ext4Inode inodeOf(const std::vector<std::byte>& slot) {
	const auto parsed = parseExt4Inode(slot);
	EXPECT_TRUE(parsed.hasValue());
	return parsed.hasValue() ? parsed.value() : Ext4Inode{};
}

TEST(Ext4Inode, ALiveRegularFileReadsBackItsFields) {
	const auto inode = inodeOf(makeInode(kRegularFileMode));
	EXPECT_TRUE(inode.isRegularFile);
	EXPECT_FALSE(inode.isDirectory);
	EXPECT_EQ(inode.sizeInBytes, kSizeLow);
	EXPECT_EQ(inode.linkCount, 1U);
}

TEST(Ext4Inode, ALiveInodeIsNeitherDeletedNorUnused) {
	const auto inode = inodeOf(makeInode(kRegularFileMode));
	EXPECT_FALSE(inode.isDeleted);
	EXPECT_FALSE(inode.isUnused);
}

TEST(Ext4Inode, TheExtentFlagIsReported) {
	EXPECT_TRUE(inodeOf(makeInode(kRegularFileMode)).usesExtents);
}

// ext2's indirect block list lives in the same 60 bytes, so the flag is the only
// thing that says which one is there.
TEST(Ext4Inode, AnInodeWithoutTheExtentFlagSaysSo) {
	auto slot = makeInode(kRegularFileMode);
	writeLe(slot, 0x20, std::uint32_t{0});
	EXPECT_FALSE(inodeOf(slot).usesExtents);
}

TEST(Ext4Inode, TheBlockMapIsHandedOnAsItLies) {
	const auto inode = inodeOf(makeInode(kRegularFileMode));
	EXPECT_EQ(inode.blockMap.front(), std::byte{0x0A});
	EXPECT_EQ(inode.blockMap.at(1), std::byte{0xF3});
}

TEST(Ext4Inode, ADirectoryIsRecognizedByItsMode) {
	const auto inode = inodeOf(makeInode(kDirectoryMode));
	EXPECT_TRUE(inode.isDirectory);
	EXPECT_FALSE(inode.isRegularFile);
}

// The undelete case: the volume cleared the link count and stamped a deletion
// time, and left size, mode, times and block map exactly where they were.
TEST(Ext4Inode, AFreedInodeKeepsEverythingButItsLinkCount) {
	auto slot = makeInode(kRegularFileMode);
	writeLe(slot, 0x1A, std::uint16_t{0});
	writeLe(slot, 0x14, kModifySeconds);
	const auto inode = inodeOf(slot);
	EXPECT_TRUE(inode.isDeleted);
	EXPECT_EQ(inode.sizeInBytes, kSizeLow);
	EXPECT_EQ(inode.deletionTime, kModifySeconds);
}

// A slot that never held a file has no type in its mode at all — which is what
// tells it from a deleted one, since both have no links.
TEST(Ext4Inode, AnInodeThatNeverHeldAnythingIsUnusedRatherThanDeleted) {
	const std::vector<std::byte> slot(kLargeInodeBytes, std::byte{0});
	const auto inode = inodeOf(slot);
	EXPECT_TRUE(inode.isUnused);
	EXPECT_EQ(inode.mode, 0U);
}

TEST(Ext4Inode, ALargeRegularFilesSizeUsesBothHalves) {
	auto slot = makeInode(kRegularFileMode);
	writeLe(slot, 0x6C, std::uint32_t{1});
	EXPECT_EQ(inodeOf(slot).sizeInBytes, (std::uint64_t{1} << 32U) + kSizeLow);
}

// For anything but a regular file that field is `i_dir_acl`. Folding it in would
// give a directory a size in the terabytes off unrelated data.
TEST(Ext4Inode, ADirectorysHighSizeFieldIsNotPartOfItsSize) {
	auto slot = makeInode(kDirectoryMode);
	writeLe(slot, 0x6C, std::uint32_t{1});
	EXPECT_EQ(inodeOf(slot).sizeInBytes, kSizeLow);
}

TEST(Ext4Inode, TimesConvertToTheLayersEpoch) {
	const auto inode = inodeOf(makeInode(kRegularFileMode));
	EXPECT_EQ(inode.timestamps.modified, kModifyTicks);
	EXPECT_EQ(inode.timestamps.accessed, kModifyTicks);
	EXPECT_EQ(inode.timestamps.created, kModifyTicks);
}

// `i_crtime` lives past the fixed 128 bytes, so a 128-byte inode has none —
// and `i_ctime`, which is there, is not a creation time and is not substituted.
TEST(Ext4Inode, AnInodeTooSmallForACreationTimeReportsNone) {
	auto slot = makeInode(kRegularFileMode);
	slot.resize(kMinInodeBytes);
	const auto inode = inodeOf(slot);
	EXPECT_EQ(inode.timestamps.created, 0U);
	EXPECT_EQ(inode.timestamps.modified, kModifyTicks);
}

// The inode is long enough, but says its extra area stops short of the field.
TEST(Ext4Inode, AnInodeWhoseExtraAreaStopsShortReportsNoCreationTime) {
	auto slot = makeInode(kRegularFileMode);
	writeLe(slot, 0x80, std::uint16_t{4});
	EXPECT_EQ(inodeOf(slot).timestamps.created, 0U);
}

TEST(Ext4Inode, AShortSlotIsOutOfRange) {
	const std::vector<std::byte> slot(kMinInodeBytes - 1, std::byte{0});
	const auto parsed = parseExt4Inode(slot);
	ASSERT_FALSE(parsed.hasValue());
	EXPECT_EQ(parsed.error().code, ErrorCode::kOutOfRange);
}

} // namespace
