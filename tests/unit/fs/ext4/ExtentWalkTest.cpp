// SPDX-License-Identifier: GPL-3.0-or-later
// story-0307: the extent tree followed off the inode and onto the volume. What
// is asserted here is mostly the refusals: `RecoveredEntry`'s extents are a
// concatenation, so a hole or an unwritten run cannot be spelled in them, and
// padding a recovered file with bytes that were never in it would be worse than
// locating nothing at all.
#include "fs/ext4/ExtentWalk.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ext4/Ext4Layout.hpp"
#include "imagegen/ext4/Ext4Records.hpp"
#include "revenant/fs/Types.hpp"
#include "support/Ext4TestVolume.hpp"

namespace {

using revenant::fs::Extent;
using revenant::fs::ext4::treeExtents;
using revenant::imagegen::putBytes;
using revenant::imagegen::putLe;
using revenant::imagegen::ext4::ExtentSpec;
using revenant::imagegen::ext4::extentTree;
using revenant::imagegen::ext4::makeExt4Layout;
using revenant::testing::emptyExt4Image;
using revenant::testing::Ext4TestVolume;

constexpr std::uint32_t kBlockSize = 1024;
constexpr std::uint32_t kDataBlock = 40;
constexpr std::uint32_t kInteriorNodeBlock = 50;

[[nodiscard]] std::vector<ExtentSpec> oneRun(std::uint32_t first, std::uint32_t count) {
	return {ExtentSpec{.firstFileBlock = 0, .blockCount = count, .firstDeviceBlock = first}};
}

// An interior node in a block of its own, pointing at a leaf node in another —
// the shape a tree takes once it outgrows the sixty bytes an inode gives it.
struct TwoLevelSpec {
	std::uint32_t indexBlock;
	std::uint32_t leafBlock;
};

void putInteriorNode(std::vector<std::byte>& image, const TwoLevelSpec& spec) {
	const auto at = static_cast<std::size_t>(makeExt4Layout().blockOffset(spec.indexBlock));
	putLe<std::uint16_t>(image, at + 0x00, 0xF30A);
	putLe<std::uint16_t>(image, at + 0x02, 1);
	putLe<std::uint16_t>(image, at + 0x04, 4);
	putLe<std::uint16_t>(image, at + 0x06, 1); // depth
	putLe<std::uint32_t>(image, at + 0x10, spec.leafBlock);
}

// The inline root of an inode's tree, as sixty bytes a test hands straight to
// the walk.
[[nodiscard]] std::vector<std::byte> inlineRoot(const std::vector<ExtentSpec>& runs) {
	return extentTree(runs);
}

[[nodiscard]] std::vector<Extent> extentsOf(
	const Ext4TestVolume& volume,
	const std::vector<std::byte>& root,
	std::uint64_t sizeBytes) {
	const auto found = treeExtents(volume.blocks(), root, sizeBytes);
	EXPECT_TRUE(found.hasValue());
	return found.hasValue() ? found.value() : std::vector<Extent>{};
}

// Most of these only need the tree; the volume under it is empty because
// nothing but the leaves' own block numbers is being asked about.
[[nodiscard]] std::vector<Extent>
extentsOnEmptyVolume(const std::vector<std::byte>& root, std::uint64_t sizeBytes) {
	const Ext4TestVolume volume{emptyExt4Image()};
	return extentsOf(volume, root, sizeBytes);
}

[[nodiscard]] bool refuses(const std::vector<std::byte>& root, std::uint64_t sizeBytes) {
	const Ext4TestVolume volume{emptyExt4Image()};
	return !treeExtents(volume.blocks(), root, sizeBytes).hasValue();
}

TEST(Ext4ExtentWalk, AnInlineLeafBecomesOneDeviceExtent) {
	const auto found = extentsOnEmptyVolume(inlineRoot(oneRun(kDataBlock, 2)), 1500);
	ASSERT_EQ(found.size(), 1U);
	EXPECT_EQ(found.front().deviceOffset, std::uint64_t{kDataBlock} * kBlockSize);
	EXPECT_EQ(found.front().lengthBytes, 1500U);
}

// Two runs that happen to touch are one extent, not two: what matters is where
// the bytes are, not how the tree spelled them.
TEST(Ext4ExtentWalk, AdjacentRunsCoalesce) {
	const std::vector<ExtentSpec> runs{
		ExtentSpec{.firstFileBlock = 0, .blockCount = 1, .firstDeviceBlock = kDataBlock},
		ExtentSpec{.firstFileBlock = 1, .blockCount = 1, .firstDeviceBlock = kDataBlock + 1}};
	const auto found = extentsOnEmptyVolume(inlineRoot(runs), 2048);
	ASSERT_EQ(found.size(), 1U);
	EXPECT_EQ(found.front().lengthBytes, 2048U);
}

TEST(Ext4ExtentWalk, SeparateRunsStaySeparate) {
	const std::vector<ExtentSpec> runs{
		ExtentSpec{.firstFileBlock = 0, .blockCount = 1, .firstDeviceBlock = kDataBlock},
		ExtentSpec{.firstFileBlock = 1, .blockCount = 1, .firstDeviceBlock = kDataBlock + 10}};
	EXPECT_EQ(extentsOnEmptyVolume(inlineRoot(runs), 2048).size(), 2U);
}

// The tree's interior node lives in a block of its own, so following it is a
// read rather than a parse — which is the whole reason the walk exists.
TEST(Ext4ExtentWalk, AnInteriorNodeIsFollowedOntoTheVolume) {
	auto image = emptyExt4Image();
	putInteriorNode(image, TwoLevelSpec{.indexBlock = kInteriorNodeBlock, .leafBlock = 51});
	putBytes(
		image,
		static_cast<std::size_t>(makeExt4Layout().blockOffset(51)),
		inlineRoot(oneRun(kDataBlock, 1)));
	std::vector<std::byte> root(60, std::byte{0});
	putLe<std::uint16_t>(root, 0x00, 0xF30A);
	putLe<std::uint16_t>(root, 0x02, 1);
	putLe<std::uint16_t>(root, 0x06, 2);
	putLe<std::uint32_t>(root, 0x10, kInteriorNodeBlock);
	const Ext4TestVolume volume{image};
	const auto found = extentsOf(volume, root, 900);
	ASSERT_EQ(found.size(), 1U);
	EXPECT_EQ(found.front().deviceOffset, std::uint64_t{kDataBlock} * kBlockSize);
}

TEST(Ext4ExtentWalk, AZeroSizedFileNeedsNoExtentsAtAll) {
	EXPECT_TRUE(extentsOnEmptyVolume(inlineRoot({}), 0).empty());
}

// A wiped tree parses as nothing at all, which is the case the journal hint
// exists for.
TEST(Ext4ExtentWalk, AWipedTreeRefusesRatherThanReturningNothing) {
	EXPECT_TRUE(refuses(std::vector<std::byte>(60, std::byte{0}), 900));
}

// Blocks allocated but never written hold whatever they last held.
TEST(Ext4ExtentWalk, AnUnwrittenRunMakesTheWholeMappingUnusable) {
	auto root = inlineRoot(oneRun(kDataBlock, 2));
	putLe<std::uint16_t>(root, 0x0C + 0x04, static_cast<std::uint16_t>(32768 + 2));
	EXPECT_TRUE(refuses(root, 1500));
}

// A sparse file's missing blocks are zeros, and a concatenation of extents
// cannot say so — so it says nothing instead.
TEST(Ext4ExtentWalk, AHoleInTheFilesBlockNumberingMakesTheMappingUnusable) {
	const std::vector<ExtentSpec> runs{
		ExtentSpec{.firstFileBlock = 1, .blockCount = 1, .firstDeviceBlock = kDataBlock}};
	EXPECT_TRUE(refuses(inlineRoot(runs), 900));
}

TEST(Ext4ExtentWalk, ARunReachingPastTheVolumeIsRefused) {
	EXPECT_TRUE(refuses(inlineRoot(oneRun(600, 2)), 1500));
}

// The last block of a file is only partly used; claiming all of it would append
// whatever the volume left there.
TEST(Ext4ExtentWalk, ExtentsAreTrimmedToTheFilesOwnSize) {
	const auto found = extentsOnEmptyVolume(inlineRoot(oneRun(kDataBlock, 2)), 1100);
	ASSERT_EQ(found.size(), 1U);
	EXPECT_EQ(found.front().lengthBytes, 1100U);
}

TEST(Ext4ExtentWalk, ASizeLargerThanTheRunsAllocateIsRefused) {
	EXPECT_TRUE(refuses(inlineRoot(oneRun(kDataBlock, 1)), 5000));
}

// An interior node that points at itself would otherwise be followed forever.
// The node budget is what ends it — and it is checked as children are *queued*,
// because one wide node's fan-out is read before any of it is walked.
TEST(Ext4ExtentWalk, ATreeThatPointsBackAtItselfIsRefusedRatherThanFollowed) {
	auto image = emptyExt4Image();
	putInteriorNode(
		image,
		TwoLevelSpec{.indexBlock = kInteriorNodeBlock, .leafBlock = kInteriorNodeBlock});
	std::vector<std::byte> root(60, std::byte{0});
	putLe<std::uint16_t>(root, 0x00, 0xF30A);
	putLe<std::uint16_t>(root, 0x02, 1);
	putLe<std::uint16_t>(root, 0x06, 2);
	putLe<std::uint32_t>(root, 0x10, kInteriorNodeBlock);
	const Ext4TestVolume volume{image};
	EXPECT_FALSE(treeExtents(volume.blocks(), root, 900).hasValue());
}

} // namespace
