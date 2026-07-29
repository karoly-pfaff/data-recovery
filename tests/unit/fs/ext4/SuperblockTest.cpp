// SPDX-License-Identifier: GPL-3.0-or-later
// story-0306: the ext4 superblock. Most of these pin down that a field is
// checked against the *volume's own* consistency rules — a bitmap block only
// addresses so many blocks, a record has to fit inside a block — rather than
// against a constant picked here.
#include "revenant/fs/ext4/Superblock.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::toLittleEndian;
using revenant::fs::ext4::Ext4Geometry;
using revenant::fs::ext4::kSuperblockBytes;
using revenant::fs::ext4::parseExt4Superblock;
using revenant::testing::invalidAt;
using revenant::testing::outOfRangeAt;
using revenant::testing::Rejection;

constexpr std::uint32_t kBlockSize = 4096;
constexpr std::uint32_t kTotalBlocks = 16'384;
constexpr std::uint32_t kTotalInodes = 2048;
constexpr std::uint32_t kBlocksPerGroup = 8192;
constexpr std::uint32_t kInodesPerGroup = 1024;
constexpr std::uint32_t kInodeSize = 256;
constexpr std::uint32_t kIncompatExtents = 0x40;
constexpr std::uint32_t kIncompat64Bit = 0x80;
constexpr std::uint32_t kLastOrphan = 27;

void writeLe(std::vector<std::byte>& block, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, block.begin() + static_cast<std::ptrdiff_t>(offset));
}

void writeCounts(std::vector<std::byte>& block) {
	writeLe(block, 0x00, kTotalInodes);
	writeLe(block, 0x04, kTotalBlocks);
	writeLe(block, 0x20, kBlocksPerGroup);
	writeLe(block, 0x28, kInodesPerGroup);
}

void writeLayout(std::vector<std::byte>& block) {
	writeLe(block, 0x14, std::uint32_t{0});
	writeLe(block, 0x18, std::uint32_t{2}); // 1024 << 2 == 4096
	writeLe(block, 0x38, std::uint16_t{0xEF53});
	writeLe(block, 0x58, static_cast<std::uint16_t>(kInodeSize));
	writeLe(block, 0x60, kIncompatExtents);
	writeLe(block, 0x64, kLastOrphan);
}

// A 4096-byte-block volume with two block groups, extents, and no 64-bit
// feature — the shape mke2fs writes for a small filesystem.
[[nodiscard]] std::vector<std::byte> makeSuperblock() {
	std::vector<std::byte> block(kSuperblockBytes, std::byte{0});
	writeCounts(block);
	writeLayout(block);
	return block;
}

// Restates a 1024-byte-block volume, where the superblock is a block of its own
// and every size around it changes with it.
void writeSmallBlockVolume(std::vector<std::byte>& block) {
	writeLe(block, 0x18, std::uint32_t{0});
	writeLe(block, 0x14, std::uint32_t{1});
	writeLe(block, 0x20, std::uint32_t{8192});
	writeLe(block, 0x28, std::uint32_t{1024});
	writeLe(block, 0x58, std::uint16_t{128});
}

[[nodiscard]] Ext4Geometry geometryOf(const std::vector<std::byte>& block) {
	const auto parsed = parseExt4Superblock(block);
	EXPECT_TRUE(parsed.hasValue());
	return parsed.hasValue() ? parsed.value() : Ext4Geometry{};
}

[[nodiscard]] Rejection rejectionOf(const std::vector<std::byte>& block) {
	return revenant::testing::rejectionOf(parseExt4Superblock(block));
}

TEST(Ext4Superblock, AKnownGoodSuperblockParsesToItsGeometry) {
	const auto geometry = geometryOf(makeSuperblock());
	EXPECT_EQ(geometry.blockSizeBytes, kBlockSize);
	EXPECT_EQ(geometry.totalBlocks, kTotalBlocks);
	EXPECT_EQ(geometry.totalInodes, kTotalInodes);
	EXPECT_EQ(geometry.inodeSizeBytes, kInodeSize);
}

TEST(Ext4Superblock, TheGroupLayoutIsDerivedFromTheVolumesOwnCounts) {
	const auto geometry = geometryOf(makeSuperblock());
	EXPECT_EQ(geometry.blocksPerGroup, kBlocksPerGroup);
	EXPECT_EQ(geometry.inodesPerGroup, kInodesPerGroup);
	EXPECT_EQ(geometry.groupCount, 2U); // 16384 blocks over 8192 per group
	EXPECT_EQ(geometry.groupDescriptorBlock, 1U);
}

// A group count is a rounded-up division: the last group is short, not absent.
TEST(Ext4Superblock, APartialLastGroupStillCounts) {
	auto block = makeSuperblock();
	writeLe(block, 0x04, std::uint32_t{kBlocksPerGroup + 1});
	EXPECT_EQ(geometryOf(block).groupCount, 2U);
}

TEST(Ext4Superblock, TheOrphanListHeadAndTheExtentFeatureAreReported) {
	const auto geometry = geometryOf(makeSuperblock());
	EXPECT_EQ(geometry.lastOrphanInode, kLastOrphan);
	EXPECT_TRUE(geometry.usesExtents);
}

// Without the 64-bit feature the descriptors are 32 bytes whatever `s_desc_size`
// holds — a volume that never had large descriptors cannot acquire them by
// leaving a stale value in a field nothing reads.
TEST(Ext4Superblock, DescriptorsAreThirtyTwoBytesWithoutTheSixtyFourBitFeature) {
	auto block = makeSuperblock();
	writeLe(block, 0xFE, std::uint16_t{64});
	EXPECT_EQ(geometryOf(block).descriptorSizeBytes, 32U);
}

TEST(Ext4Superblock, ASixtyFourBitVolumeStatesItsDescriptorSizeAndItsHighBlockCount) {
	auto block = makeSuperblock();
	writeLe(block, 0x60, kIncompatExtents | kIncompat64Bit);
	writeLe(block, 0xFE, std::uint16_t{64});
	writeLe(block, 0x150, std::uint32_t{1});
	const auto geometry = geometryOf(block);
	EXPECT_EQ(geometry.descriptorSizeBytes, 64U);
	EXPECT_EQ(geometry.totalBlocks, (std::uint64_t{1} << 32U) + kTotalBlocks);
}

TEST(Ext4Superblock, AShortInputIsOutOfRange) {
	const std::vector<std::byte> block(kSuperblockBytes - 1, std::byte{0});
	EXPECT_EQ(rejectionOf(block), outOfRangeAt(kSuperblockBytes - 1));
}

TEST(Ext4Superblock, AWrongMagicIsRejectedAtItsOffset) {
	auto block = makeSuperblock();
	writeLe(block, 0x38, std::uint16_t{0xEF54});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x38));
}

// The magic is only sixteen bits, so recognition needs the block-size shift with
// it — otherwise two coincidental bytes would let ext4 claim a RAW volume.
TEST(Ext4Superblock, ABlockShiftLargerThanExtFourCanExpressIsRejected) {
	auto block = makeSuperblock();
	writeLe(block, 0x18, std::uint32_t{7});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x18));
}

TEST(Ext4Superblock, AFirstDataBlockThatContradictsTheBlockSizeIsRejected) {
	auto block = makeSuperblock();
	writeLe(block, 0x14, std::uint32_t{1});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x14));
}

// On a 1024-byte-block volume the superblock is a block of its own, so the
// answer flips — which is the point of checking it against the block size.
TEST(Ext4Superblock, AThousandTwentyFourByteBlockVolumeStartsItsDataAtBlockOne) {
	auto block = makeSuperblock();
	writeSmallBlockVolume(block);
	const auto geometry = geometryOf(block);
	EXPECT_EQ(geometry.blockSizeBytes, 1024U);
	EXPECT_EQ(geometry.groupDescriptorBlock, 2U);
}

TEST(Ext4Superblock, AZeroBlocksPerGroupIsRejectedAtItsOffset) {
	auto block = makeSuperblock();
	writeLe(block, 0x20, std::uint32_t{0});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x20));
}

// One block group's block bitmap is one block, so it cannot address more blocks
// than that block has bits.
TEST(Ext4Superblock, MoreBlocksPerGroupThanOneBitmapBlockCanAddressIsRejected) {
	auto block = makeSuperblock();
	writeLe(block, 0x20, std::uint32_t{(kBlockSize * 8) + 1});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x20));
}

TEST(Ext4Superblock, AnOverLargeInodesPerGroupIsRejectedAtItsOffset) {
	auto block = makeSuperblock();
	writeLe(block, 0x28, std::uint32_t{(kBlockSize * 8) + 1});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x28));
}

TEST(Ext4Superblock, AnInodeSmallerThanExtTwoEverWroteIsRejected) {
	auto block = makeSuperblock();
	writeLe(block, 0x58, std::uint16_t{64});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x58));
}

TEST(Ext4Superblock, AnInodeSizeThatIsNotAPowerOfTwoIsRejected) {
	auto block = makeSuperblock();
	writeLe(block, 0x58, std::uint16_t{200});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x58));
}

// An inode that will not fit in a block cannot be read at all.
TEST(Ext4Superblock, AnInodeLargerThanABlockIsRejected) {
	auto block = makeSuperblock();
	writeLe(block, 0x58, std::uint16_t{8192});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x58));
}

TEST(Ext4Superblock, ASixtyFourBitVolumesUndersizedDescriptorIsRejected) {
	auto block = makeSuperblock();
	writeLe(block, 0x60, kIncompatExtents | kIncompat64Bit);
	writeLe(block, 0xFE, std::uint16_t{32});
	EXPECT_EQ(rejectionOf(block), invalidAt(0xFE));
}

TEST(Ext4Superblock, AZeroInodeCountIsRejectedAtItsOffset) {
	auto block = makeSuperblock();
	writeLe(block, 0x00, std::uint32_t{0});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x00));
}

TEST(Ext4Superblock, AZeroBlockCountIsRejectedAtItsOffset) {
	auto block = makeSuperblock();
	writeLe(block, 0x04, std::uint32_t{0});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x04));
}

// A 1024-byte-block volume holding only its own superblock has no data at all.
TEST(Ext4Superblock, AVolumeWithNoBlockPastItsSuperblockIsRejected) {
	auto block = makeSuperblock();
	writeSmallBlockVolume(block);
	writeLe(block, 0x04, std::uint32_t{1});
	EXPECT_EQ(rejectionOf(block), invalidAt(0x04));
}

} // namespace
