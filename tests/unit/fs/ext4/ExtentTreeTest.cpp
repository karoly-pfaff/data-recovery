// SPDX-License-Identifier: GPL-3.0-or-later
// story-0034: one extent-tree node. The rejections are the point: a node states
// its own depth and entry count, and both are numbers off a disk that decide how
// far a walk will go and how much of the node it will read.
#include "revenant/fs/ext4/ExtentTree.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::toLittleEndian;
using revenant::fs::ext4::Ext4Extent;
using revenant::fs::ext4::kExtentEntryBytes;
using revenant::fs::ext4::kExtentHeaderBytes;
using revenant::fs::ext4::parseExtentHeader;
using revenant::fs::ext4::parseExtentIndices;
using revenant::fs::ext4::parseExtentLeaves;
using revenant::testing::invalidAt;
using revenant::testing::outOfRangeAt;
using revenant::testing::Rejection;

// The 60 bytes an inode gives its tree: a header and four entries.
constexpr std::size_t kInlineNodeBytes = 60;
constexpr std::uint16_t kMagic = 0xF30A;
constexpr std::uint32_t kMaxInitializedLength = 32768;

void writeLe(std::vector<std::byte>& node, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, node.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] std::vector<std::byte> makeNode(std::uint16_t entries, std::uint16_t depth) {
	std::vector<std::byte> node(kInlineNodeBytes, std::byte{0});
	writeLe(node, 0x00, kMagic);
	writeLe(node, 0x02, entries);
	writeLe(node, 0x04, std::uint16_t{4});
	writeLe(node, 0x06, depth);
	return node;
}

// One leaf entry: `count` blocks of the file starting at `fileBlock` live at
// `deviceBlock` on the volume.
struct LeafSpec {
	std::uint16_t at;
	std::uint32_t fileBlock;
	std::uint16_t count;
	std::uint32_t deviceBlock;
};

void writeLeaf(std::vector<std::byte>& node, const LeafSpec& spec) {
	const std::size_t base = kExtentHeaderBytes + (std::size_t{spec.at} * kExtentEntryBytes);
	writeLe(node, base + 0x00, spec.fileBlock);
	writeLe(node, base + 0x04, spec.count);
	writeLe(node, base + 0x08, spec.deviceBlock);
}

void writeIndex(std::vector<std::byte>& node, std::uint32_t fileBlock, std::uint32_t nodeBlock) {
	writeLe(node, kExtentHeaderBytes + 0x00, fileBlock);
	writeLe(node, kExtentHeaderBytes + 0x04, nodeBlock);
}

[[nodiscard]] std::vector<Ext4Extent> leavesOf(const std::vector<std::byte>& node) {
	const auto parsed = parseExtentLeaves(node);
	EXPECT_TRUE(parsed.hasValue());
	return parsed.hasValue() ? parsed.value() : std::vector<Ext4Extent>{};
}

[[nodiscard]] Rejection rejectionOf(const std::vector<std::byte>& node) {
	return revenant::testing::rejectionOf(parseExtentHeader(node));
}

TEST(Ext4ExtentTree, AHeaderReadsBackItsCounts) {
	const auto parsed = parseExtentHeader(makeNode(2, 0));
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_EQ(parsed.value().entries, 2U);
	EXPECT_EQ(parsed.value().max, 4U);
	EXPECT_EQ(parsed.value().depth, 0U);
}

TEST(Ext4ExtentTree, ALeafNodeYieldsItsExtentsInOrder) {
	auto node = makeNode(2, 0);
	writeLeaf(node, LeafSpec{.at = 0, .fileBlock = 0, .count = 4, .deviceBlock = 100});
	writeLeaf(node, LeafSpec{.at = 1, .fileBlock = 4, .count = 2, .deviceBlock = 200});
	const auto leaves = leavesOf(node);
	ASSERT_EQ(leaves.size(), 2U);
	EXPECT_EQ(leaves.front().firstDeviceBlock, 100U);
	EXPECT_EQ(leaves.back().firstFileBlock, 4U);
	EXPECT_EQ(leaves.back().blockCount, 2U);
}

TEST(Ext4ExtentTree, AnExtentIsInitializedUntilItsLengthSaysOtherwise) {
	auto node = makeNode(1, 0);
	writeLeaf(node, LeafSpec{.at = 0, .fileBlock = 0, .count = 4, .deviceBlock = 100});
	EXPECT_TRUE(leavesOf(node).front().initialized);
}

// Blocks allocated but never written hold whatever they last held, so the flag
// travels with the length rather than being folded into it.
TEST(Ext4ExtentTree, AnUnwrittenExtentReportsItsRealLengthAndSaysItIsUnwritten) {
	auto node = makeNode(1, 0);
	writeLeaf(
		node,
		LeafSpec{
			.at = 0,
			.fileBlock = 0,
			.count = static_cast<std::uint16_t>(kMaxInitializedLength + 3),
			.deviceBlock = 100});
	const auto leaf = leavesOf(node).front();
	EXPECT_EQ(leaf.blockCount, 3U);
	EXPECT_FALSE(leaf.initialized);
}

// Exactly 32768 is the largest an initialized extent may be, not the first
// unwritten one.
TEST(Ext4ExtentTree, TheLargestInitializedLengthIsStillInitialized) {
	auto node = makeNode(1, 0);
	writeLeaf(
		node,
		LeafSpec{
			.at = 0,
			.fileBlock = 0,
			.count = static_cast<std::uint16_t>(kMaxInitializedLength),
			.deviceBlock = 100});
	const auto leaf = leavesOf(node).front();
	EXPECT_EQ(leaf.blockCount, kMaxInitializedLength);
	EXPECT_TRUE(leaf.initialized);
}

TEST(Ext4ExtentTree, ALeafsHighStartHalfIsPartOfItsBlockNumber) {
	auto node = makeNode(1, 0);
	writeLeaf(node, LeafSpec{.at = 0, .fileBlock = 0, .count = 1, .deviceBlock = 7});
	writeLe(node, kExtentHeaderBytes + 0x06, std::uint16_t{2});
	EXPECT_EQ(leavesOf(node).front().firstDeviceBlock, (std::uint64_t{2} << 32U) + 7U);
}

TEST(Ext4ExtentTree, AnInteriorNodeYieldsItsIndices) {
	auto node = makeNode(1, 1);
	writeIndex(node, 0, 500);
	const auto parsed = parseExtentIndices(node);
	ASSERT_TRUE(parsed.hasValue());
	ASSERT_EQ(parsed.value().size(), 1U);
	EXPECT_EQ(parsed.value().front().nodeBlock, 500U);
}

// Read as the other kind, an index's block number would address a data block and
// an extent's a tree node. Both would be believed, so neither is allowed.
TEST(Ext4ExtentTree, AnInteriorNodeIsNotReadAsALeaf) {
	EXPECT_EQ(revenant::testing::rejectionOf(parseExtentLeaves(makeNode(1, 1))), invalidAt(0x06));
}

TEST(Ext4ExtentTree, ALeafIsNotReadAsAnInteriorNode) {
	EXPECT_EQ(revenant::testing::rejectionOf(parseExtentIndices(makeNode(1, 0))), invalidAt(0x06));
}

TEST(Ext4ExtentTree, AWrongMagicIsRejectedAtItsOffset) {
	auto node = makeNode(1, 0);
	writeLe(node, 0x00, std::uint16_t{0xF30B});
	EXPECT_EQ(rejectionOf(node), invalidAt(0x00));
}

TEST(Ext4ExtentTree, ADepthDeeperThanExtFourCanBuildIsRejected) {
	EXPECT_EQ(rejectionOf(makeNode(1, 6)), invalidAt(0x06));
}

// The node is 60 bytes: a header and four entries. Five will not fit, and
// believing the count would read past the inode.
TEST(Ext4ExtentTree, AnEntryCountTheNodeHasNoRoomForIsRejected) {
	EXPECT_EQ(rejectionOf(makeNode(5, 0)), invalidAt(0x02));
}

TEST(Ext4ExtentTree, AShortNodeIsOutOfRange) {
	const std::vector<std::byte> node(kExtentHeaderBytes - 1, std::byte{0});
	EXPECT_EQ(rejectionOf(node), outOfRangeAt(node.size()));
}

} // namespace
