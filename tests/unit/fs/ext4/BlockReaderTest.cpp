// SPDX-License-Identifier: GPL-3.0-or-later
// story-0307: reading the bytes a set of extents covers. The cap is the point:
// a file's stated size is data like any other, and one crafted run reaching for
// gigabytes must be cut off inside itself rather than between extents.
#include "fs/ext4/BlockReader.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/fs/Types.hpp"
#include "support/Ext4TestVolume.hpp"

namespace {

using revenant::fs::Extent;
using revenant::fs::ext4::readExtents;
using revenant::testing::emptyExt4Image;
using revenant::testing::Ext4TestVolume;

constexpr std::uint32_t kBlockSize = 1024;
constexpr std::uint32_t kDataBlock = 40;

[[nodiscard]] std::size_t readBytes(const std::vector<Extent>& extents, std::size_t capBytes) {
	const Ext4TestVolume volume{emptyExt4Image()};
	const auto bytes = readExtents(volume.blocks(), extents, capBytes);
	EXPECT_TRUE(bytes.hasValue());
	return bytes.hasValue() ? bytes.value().size() : 0;
}

[[nodiscard]] Extent runAt(std::uint32_t block, std::uint64_t lengthBytes) {
	return Extent{.deviceOffset = std::uint64_t{block} * kBlockSize, .lengthBytes = lengthBytes};
}

TEST(Ext4BlockReader, ReadsEveryExtentItIsGiven) {
	EXPECT_EQ(readBytes({runAt(kDataBlock, 1024), runAt(kDataBlock + 4, 512)}, 4096), 1536U);
}

TEST(Ext4BlockReader, NoExtentsReadNoBytes) {
	EXPECT_EQ(readBytes({}, 4096), 0U);
}

TEST(Ext4BlockReader, StopsAtTheCapBetweenExtents) {
	EXPECT_EQ(readBytes({runAt(kDataBlock, 1024), runAt(kDataBlock + 4, 1024)}, 1024), 1024U);
}

// The one that matters: a single extent claiming more than the cap is cut to it
// rather than allocated whole and trimmed afterwards.
TEST(Ext4BlockReader, CutsAnOverLongExtentToTheCapRatherThanAllocatingIt) {
	EXPECT_EQ(readBytes({runAt(kDataBlock, 1U << 30U)}, 2048), 2048U);
}

TEST(Ext4BlockReader, AnExtentReachingPastTheVolumeIsAReadFailure) {
	const Ext4TestVolume volume{emptyExt4Image()};
	const std::vector<Extent> extents{runAt(1'000'000, 1024)};
	EXPECT_FALSE(readExtents(volume.blocks(), extents, 4096).hasValue());
}

} // namespace
