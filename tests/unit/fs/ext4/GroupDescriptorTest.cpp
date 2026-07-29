// SPDX-License-Identifier: GPL-3.0-or-later
// story-0034: one block group descriptor. A walk needs exactly one thing from
// it — where that group's inode table starts — and on a 64-bit volume half of
// that block number lives in the room the wider descriptor was added for.
#include "revenant/fs/ext4/GroupDescriptor.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::toLittleEndian;
using revenant::fs::ext4::kSmallDescriptorBytes;
using revenant::fs::ext4::kWideDescriptorBytes;
using revenant::fs::ext4::parseGroupDescriptor;
using revenant::testing::rejectionOf;

constexpr std::uint32_t kInodeTableBlock = 1024;

void writeLe(std::vector<std::byte>& slot, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, slot.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] std::vector<std::byte> makeDescriptor(std::size_t descriptorBytes) {
	std::vector<std::byte> slot(descriptorBytes, std::byte{0});
	writeLe(slot, 0x08, kInodeTableBlock);
	return slot;
}

TEST(Ext4GroupDescriptor, AThirtyTwoByteDescriptorNamesItsInodeTable) {
	const auto parsed =
		parseGroupDescriptor(makeDescriptor(kSmallDescriptorBytes), kSmallDescriptorBytes);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_EQ(parsed.value().inodeTableBlock, kInodeTableBlock);
}

TEST(Ext4GroupDescriptor, AWideDescriptorFoldsInItsHighHalf) {
	auto slot = makeDescriptor(kWideDescriptorBytes);
	writeLe(slot, 0x28, std::uint32_t{3});
	const auto parsed = parseGroupDescriptor(slot, kWideDescriptorBytes);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_EQ(parsed.value().inodeTableBlock, (std::uint64_t{3} << 32U) + kInodeTableBlock);
}

// The high half is only a block number on a volume whose descriptors were
// widened for it; on a narrow one those bytes belong to the next descriptor.
TEST(Ext4GroupDescriptor, ANarrowDescriptorNeverReadsPastItself) {
	auto slot = makeDescriptor(kWideDescriptorBytes);
	writeLe(slot, 0x28, std::uint32_t{3});
	const auto parsed = parseGroupDescriptor(slot, kSmallDescriptorBytes);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_EQ(parsed.value().inodeTableBlock, kInodeTableBlock);
}

TEST(Ext4GroupDescriptor, ADescriptorSizeNoExtFourVolumeHasIsRejected) {
	EXPECT_EQ(
		rejectionOf(parseGroupDescriptor(makeDescriptor(kSmallDescriptorBytes), 16)),
		revenant::testing::invalidAt(16));
}

TEST(Ext4GroupDescriptor, AShortSlotIsOutOfRange) {
	const std::vector<std::byte> slot(kSmallDescriptorBytes - 1, std::byte{0});
	EXPECT_EQ(
		rejectionOf(parseGroupDescriptor(slot, kSmallDescriptorBytes)),
		revenant::testing::outOfRangeAt(slot.size()));
}

} // namespace
