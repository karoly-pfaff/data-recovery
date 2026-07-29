// SPDX-License-Identifier: GPL-3.0-or-later
// story-0042: the block cache. Two things are asserted throughout — that it
// hands back exactly what the source holds, and that it *does not read* what it
// already has. The second is what the cache is for, and it is invisible unless
// the source counts its reads.
#include "revenant/core/io/CachingDevice.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "support/FaultyDevice.hpp"

namespace {

using revenant::CacheShape;
using revenant::CachingDevice;
using revenant::ErrorCode;
using revenant::testing::Fault;
using revenant::testing::FaultyDevice;

constexpr std::uint32_t kSector = 512;
constexpr std::size_t kBlock = 1024;
constexpr std::size_t kDeviceBytes = 5000;

[[nodiscard]] std::vector<std::byte> countingBytes(std::size_t count) {
	std::vector<std::byte> bytes(count);
	for (std::size_t at = 0; at < count; ++at) {
		bytes.at(at) = static_cast<std::byte>(static_cast<std::uint8_t>(at & 0xFFU));
	}
	return bytes;
}

[[nodiscard]] std::vector<std::byte> readOf(revenant::BlockDevice& device, std::size_t count) {
	std::vector<std::byte> buffer(count);
	const auto read = device.readAt(0, buffer);
	buffer.resize(read.hasValue() ? read.value() : 0);
	return buffer;
}

// Touches `count` consecutive blocks, which is what makes a small cache evict.
[[nodiscard]] bool sweptBlocks(revenant::BlockDevice& device, std::uint64_t count) {
	std::vector<std::byte> buffer(16);
	for (std::uint64_t block = 0; block < count; ++block) {
		if (!device.readAt(block * kBlock, buffer).hasValue()) {
			return false;
		}
	}
	return true;
}

constexpr CacheShape kTwoBlocks{.blockBytes = kBlock, .blockCount = 2};

TEST(CachingDevice, ReportsTheSourcesSizeAndSectorSize) {
	FaultyDevice source{countingBytes(kDeviceBytes), kSector, {}};
	CachingDevice cache{source, kTwoBlocks};
	EXPECT_EQ(cache.sizeInBytes(), kDeviceBytes);
	EXPECT_EQ(cache.sectorSize(), kSector);
}

TEST(CachingDevice, HandsBackExactlyWhatTheSourceHolds) {
	const auto content = countingBytes(kDeviceBytes);
	FaultyDevice source{content, kSector, {}};
	CachingDevice cache{source, kTwoBlocks};
	std::vector<std::byte> buffer(300);
	const auto read = cache.readAt(1500, buffer);
	ASSERT_TRUE(read.hasValue());
	ASSERT_EQ(read.value(), 300U);
	const auto expected = std::span{content}.subspan(1500, 300);
	EXPECT_TRUE(std::ranges::equal(buffer, expected));
}

// The whole point: the second read of a range costs the device nothing.
TEST(CachingDevice, DoesNotReadTheSourceTwiceForTheSameBlock) {
	FaultyDevice source{countingBytes(kDeviceBytes), kSector, {}};
	CachingDevice cache{source, kTwoBlocks};
	std::vector<std::byte> buffer(64);
	ASSERT_TRUE(cache.readAt(100, buffer).hasValue());
	const auto afterFirst = source.reads();
	ASSERT_TRUE(cache.readAt(200, buffer).hasValue());
	EXPECT_EQ(source.reads(), afterFirst);
}

TEST(CachingDevice, ServesAReadThatSpansTwoBlocksFromBoth) {
	const auto content = countingBytes(kDeviceBytes);
	FaultyDevice source{content, kSector, {}};
	CachingDevice cache{source, kTwoBlocks};
	std::vector<std::byte> buffer(200);
	const auto read = cache.readAt(kBlock - 100, buffer);
	ASSERT_TRUE(read.hasValue());
	ASSERT_EQ(read.value(), 200U);
	EXPECT_TRUE(std::ranges::equal(buffer, std::span{content}.subspan(kBlock - 100, 200)));
	EXPECT_EQ(cache.heldBlocks(), 2U);
}

// The last block of a device is short, and what it does not hold must not come
// back as zeros that look like data.
TEST(CachingDevice, AReadPastTheEndIsShort) {
	FaultyDevice source{countingBytes(kDeviceBytes), kSector, {}};
	CachingDevice cache{source, kTwoBlocks};
	std::vector<std::byte> buffer(1000);
	const auto read = cache.readAt(kDeviceBytes - 200, buffer);
	ASSERT_TRUE(read.hasValue());
	EXPECT_EQ(read.value(), 200U);
}

TEST(CachingDevice, AReadEntirelyPastTheEndReadsNothing) {
	FaultyDevice source{countingBytes(kDeviceBytes), kSector, {}};
	CachingDevice cache{source, kTwoBlocks};
	std::vector<std::byte> buffer(100);
	EXPECT_EQ(cache.readAt(kDeviceBytes + 10, buffer).value(), 0U);
}

TEST(CachingDevice, NeverHoldsMoreBlocksThanItWasGiven) {
	FaultyDevice source{countingBytes(kDeviceBytes), kSector, {}};
	CachingDevice cache{source, kTwoBlocks};
	ASSERT_TRUE(sweptBlocks(cache, 4));
	EXPECT_EQ(cache.heldBlocks(), 2U);
}

// The least recently used block is the one that goes, so a range kept warm by
// being re-read survives a sweep past it.
TEST(CachingDevice, EvictsTheLeastRecentlyUsedBlock) {
	FaultyDevice source{countingBytes(kDeviceBytes), kSector, {}};
	CachingDevice cache{source, kTwoBlocks};
	std::vector<std::byte> buffer(16);
	ASSERT_TRUE(cache.readAt(0, buffer).hasValue());
	ASSERT_TRUE(cache.readAt(kBlock, buffer).hasValue());
	ASSERT_TRUE(cache.readAt(0, buffer).hasValue());
	ASSERT_TRUE(cache.readAt(2 * kBlock, buffer).hasValue());
	const auto beforeReread = source.reads();
	ASSERT_TRUE(cache.readAt(0, buffer).hasValue());
	EXPECT_EQ(source.reads(), beforeReread);
}

// Policy, not on-disk data: an unusable shape is clamped rather than refused.
TEST(CachingDevice, ClampsABlockSizeBelowASector) {
	const auto content = countingBytes(kDeviceBytes);
	FaultyDevice source{content, kSector, {}};
	CachingDevice cache{source, CacheShape{.blockBytes = 100, .blockCount = 0}};
	EXPECT_TRUE(std::ranges::equal(readOf(cache, kDeviceBytes), content));
	EXPECT_EQ(cache.heldBlocks(), 1U);
}

TEST(CachingDevice, ClampsABlockSizeThatIsNotAPowerOfTwo) {
	const auto content = countingBytes(kDeviceBytes);
	FaultyDevice source{content, kSector, {}};
	CachingDevice cache{source, CacheShape{.blockBytes = 1500, .blockCount = 8}};
	EXPECT_TRUE(std::ranges::equal(readOf(cache, kDeviceBytes), content));
}

TEST(CachingDevice, PassesASourceFaultThrough) {
	FaultyDevice source{
		countingBytes(kDeviceBytes),
		kSector,
		{Fault{.offsetBytes = 2048, .lengthBytes = 512}}};
	CachingDevice cache{source, kTwoBlocks};
	std::vector<std::byte> buffer(16);
	const auto read = cache.readAt(2100, buffer);
	ASSERT_FALSE(read.hasValue());
	EXPECT_EQ(read.error().code, ErrorCode::kIoFailure);
}

} // namespace
