// SPDX-License-Identifier: GPL-3.0-or-later
// story-0035: jbd2, read and never replayed. The refusals matter most: a
// journal's feature flags decide how wide a descriptor tag is, and reading one
// shape as another yields block numbers that address the wrong blocks entirely
// — plausibly, and silently.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fs/ext4/JournalFormat.hpp"
#include "imagegen/ByteWriter.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::fs::ext4::JournalHead;
using revenant::fs::ext4::parseDescriptorTags;
using revenant::fs::ext4::parseJournalSuperblock;
using revenant::imagegen::putBe;
using revenant::testing::invalidAt;
using revenant::testing::rejectionOf;

constexpr std::size_t kBlockBytes = 1024;
constexpr std::uint32_t kJournalMagic = 0xC03B'3998;
constexpr std::uint32_t kDescriptorType = 1;
constexpr std::uint32_t kSuperblockType = 4;
constexpr std::uint16_t kSameUuidAndLast = 0x0A;
constexpr std::uint16_t kSameUuid = 0x02;

void putHeader(std::vector<std::byte>& block, std::uint32_t blockType) {
	putBe<std::uint32_t>(block, 0x00, kJournalMagic);
	putBe<std::uint32_t>(block, 0x04, blockType);
	putBe<std::uint32_t>(block, 0x08, 1);
}

[[nodiscard]] std::vector<std::byte> makeSuperblock(std::uint32_t features) {
	std::vector<std::byte> block(kBlockBytes, std::byte{0});
	putHeader(block, kSuperblockType);
	putBe<std::uint32_t>(block, 0x0C, kBlockBytes);
	putBe<std::uint32_t>(block, 0x10, 32); // s_maxlen
	putBe<std::uint32_t>(block, 0x14, 1);  // s_first
	putBe<std::uint32_t>(block, 0x28, features);
	return block;
}

[[nodiscard]] JournalHead headOf(const std::vector<std::byte>& block) {
	const auto parsed = parseJournalSuperblock(block);
	EXPECT_TRUE(parsed.hasValue());
	return parsed.hasValue() ? parsed.value() : JournalHead{};
}

// One classic tag: an eight-byte record, and sixteen more bytes of UUID after it
// unless the tag says it shares the transaction's.
struct TagSpec {
	std::size_t at;
	std::uint32_t fileSystemBlock;
	std::uint16_t flags;
};

void putTag(std::vector<std::byte>& block, const TagSpec& spec) {
	putBe<std::uint32_t>(block, spec.at + 0x00, spec.fileSystemBlock);
	putBe<std::uint16_t>(block, spec.at + 0x06, spec.flags);
}

TEST(Ext4Journal, AJournalSuperblockReadsBackItsGeometry) {
	const auto head = headOf(makeSuperblock(0));
	EXPECT_EQ(head.blockSizeBytes, kBlockBytes);
	EXPECT_EQ(head.maxBlocks, 32U);
	EXPECT_EQ(head.firstBlock, 1U);
}

// Three tag shapes, chosen by two feature bits. Getting this wrong is how a
// reader invents block numbers.
TEST(Ext4Journal, TheTagWidthFollowsTheJournalsFeatures) {
	EXPECT_EQ(headOf(makeSuperblock(0)).tagBytes, 8U);
	EXPECT_EQ(headOf(makeSuperblock(0x2)).tagBytes, 12U);  // 64-bit block numbers
	EXPECT_EQ(headOf(makeSuperblock(0x10)).tagBytes, 16U); // checksum v3
}

// A checksummed journal keeps four bytes at the end of a descriptor block that
// are not a tag, and reading them as one would invent a block.
TEST(Ext4Journal, AChecksummedJournalReservesItsBlockTail) {
	EXPECT_FALSE(headOf(makeSuperblock(0)).hasBlockTail);
	EXPECT_TRUE(headOf(makeSuperblock(0x8)).hasBlockTail);
}

TEST(Ext4Journal, AWrongMagicIsRejectedAtItsOffset) {
	auto block = makeSuperblock(0);
	putBe<std::uint32_t>(block, 0x00, 0xC03B'3999);
	EXPECT_EQ(rejectionOf(parseJournalSuperblock(block)), invalidAt(0x00));
}

TEST(Ext4Journal, ABlockThatIsNotASuperblockIsRejectedAtItsTypeField) {
	auto block = makeSuperblock(0);
	putBe<std::uint32_t>(block, 0x04, kDescriptorType);
	EXPECT_EQ(rejectionOf(parseJournalSuperblock(block)), invalidAt(0x04));
}

// A journal is read the way it was written or not read at all: a feature that
// might change the tag layout is refused whole, and the volume walks on without
// the hint.
TEST(Ext4Journal, AFeatureThisBuildDoesNotImplementIsRefused) {
	EXPECT_EQ(rejectionOf(parseJournalSuperblock(makeSuperblock(0x40))), invalidAt(0x28));
}

TEST(Ext4Journal, ADescriptorBlockNamesTheBlocksThatFollowIt) {
	std::vector<std::byte> block(kBlockBytes, std::byte{0});
	putHeader(block, kDescriptorType);
	putTag(block, TagSpec{.at = 0x0C, .fileSystemBlock = 8, .flags = kSameUuid});
	putTag(block, TagSpec{.at = 0x14, .fileSystemBlock = 9, .flags = kSameUuidAndLast});
	const auto tags = parseDescriptorTags(block, headOf(makeSuperblock(0)));
	ASSERT_TRUE(tags.hasValue());
	ASSERT_EQ(tags.value().size(), 2U);
	EXPECT_EQ(tags.value().front().fileSystemBlock, 8U);
	EXPECT_EQ(tags.value().front().blocksAfterDescriptor, 1U);
	EXPECT_EQ(tags.value().back().blocksAfterDescriptor, 2U);
}

TEST(Ext4Journal, ATagWithoutTheSharedUuidFlagIsFollowedByItsUuid) {
	std::vector<std::byte> block(kBlockBytes, std::byte{0});
	putHeader(block, kDescriptorType);
	putTag(block, TagSpec{.at = 0x0C, .fileSystemBlock = 8, .flags = 0});
	putTag(block, TagSpec{.at = 0x0C + 8 + 16, .fileSystemBlock = 9, .flags = kSameUuidAndLast});
	const auto tags = parseDescriptorTags(block, headOf(makeSuperblock(0)));
	ASSERT_TRUE(tags.hasValue());
	ASSERT_EQ(tags.value().size(), 2U);
	EXPECT_EQ(tags.value().back().fileSystemBlock, 9U);
}

TEST(Ext4Journal, ABlockThatIsNotADescriptorIsRejected) {
	std::vector<std::byte> block(kBlockBytes, std::byte{0});
	putHeader(block, kSuperblockType);
	EXPECT_EQ(rejectionOf(parseDescriptorTags(block, headOf(makeSuperblock(0)))), invalidAt(0x04));
}

TEST(Ext4Journal, AShortBlockIsOutOfRange) {
	const std::vector<std::byte> block(8, std::byte{0});
	EXPECT_EQ(
		rejectionOf(parseDescriptorTags(block, headOf(makeSuperblock(0)))),
		revenant::testing::outOfRangeAt(block.size()));
}

} // namespace
