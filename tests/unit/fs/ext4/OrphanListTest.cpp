// SPDX-License-Identifier: GPL-3.0-or-later
// story-0307: the orphan chain. Every link is a number off a disk, read out of a
// field that on a *freed* inode means something else entirely — so most of what
// is asserted here is that a crafted chain stops.
#include "fs/ext4/OrphanList.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "imagegen/ext4/Ext4Layout.hpp"
#include "imagegen/ext4/Ext4Metadata.hpp"
#include "imagegen/ext4/Ext4Records.hpp"
#include "support/Ext4TestVolume.hpp"

namespace {

using revenant::fs::ext4::orphanInodes;
using revenant::imagegen::ext4::InodeSpec;
using revenant::imagegen::ext4::makeExt4Layout;
using revenant::imagegen::ext4::putExt4Inode;
using revenant::testing::emptyExt4Image;
using revenant::testing::Ext4TestVolume;

constexpr std::uint16_t kRegularFileMode = 0x81A4;

// One orphan: freed, so no links, and its `i_dtime` holds the next orphan's
// number rather than a time.
struct OrphanSpec {
	std::uint32_t number;
	std::uint32_t next;
};

[[nodiscard]] std::vector<std::byte> volumeWith(const std::vector<OrphanSpec>& orphans) {
	auto image = emptyExt4Image();
	const auto layout = makeExt4Layout();
	for (const OrphanSpec& orphan : orphans) {
		putExt4Inode(
			image,
			layout,
			orphan.number,
			InodeSpec{
				.mode = kRegularFileMode,
				.links = 0,
				.sizeInBytes = 100,
				.deletionTime = orphan.next,
				.runs = {}});
	}
	return image;
}

[[nodiscard]] std::vector<std::uint32_t>
chainFrom(const std::vector<OrphanSpec>& orphans, std::uint32_t head) {
	const Ext4TestVolume volume{volumeWith(orphans)};
	return orphanInodes(volume.inodes(), head);
}

TEST(Ext4OrphanList, AnEmptyListYieldsNothing) {
	EXPECT_TRUE(chainFrom({}, 0).empty());
}

TEST(Ext4OrphanList, AChainOfTwoIsFollowedInOrder) {
	const auto found =
		chainFrom({OrphanSpec{.number = 20, .next = 21}, OrphanSpec{.number = 21, .next = 0}}, 20);
	ASSERT_EQ(found.size(), 2U);
	EXPECT_EQ(found.front(), 20U);
	EXPECT_EQ(found.back(), 21U);
}

// The field the chain runs through is one a deletion writes, so a volume can
// spell a cycle in it as easily as a list.
TEST(Ext4OrphanList, AChainThatPointsAtItselfEndsThere) {
	const auto found = chainFrom({OrphanSpec{.number = 20, .next = 20}}, 20);
	EXPECT_EQ(found.size(), 1U);
}

TEST(Ext4OrphanList, ATwoInodeCycleEndsRatherThanRepeating) {
	const auto found =
		chainFrom({OrphanSpec{.number = 20, .next = 21}, OrphanSpec{.number = 21, .next = 20}}, 20);
	EXPECT_EQ(found.size(), 2U);
}

TEST(Ext4OrphanList, AHeadTheVolumeCouldNotHaveYieldsNothing) {
	EXPECT_TRUE(chainFrom({}, 9999).empty());
}

// An inode that never held anything cannot be an orphan, and the number that
// would follow it came out of the same unwritten bytes.
TEST(Ext4OrphanList, AChainRunningIntoAnUnusedInodeStopsThere) {
	const auto found = chainFrom({OrphanSpec{.number = 20, .next = 30}}, 20);
	ASSERT_EQ(found.size(), 2U);
	EXPECT_EQ(found.back(), 30U);
}

} // namespace
