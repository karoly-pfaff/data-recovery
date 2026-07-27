// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/volume/PartitionView.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "revenant/core/Error.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::ErrorCode;
using revenant::testing::InMemoryDevice;
using revenant::volume::PartitionView;

constexpr std::uint32_t kSector = 512;

std::vector<std::byte> makeBytes(std::size_t count) {
	std::vector<std::byte> bytes(count);
	for (std::size_t i = 0; i < count; ++i) {
		bytes.at(i) = static_cast<std::byte>(static_cast<std::uint8_t>(i & 0xFFU));
	}
	return bytes;
}

TEST(PartitionView, ReportsViewSizeAndSectorSize) {
	auto parent = InMemoryDevice{makeBytes(1024), kSector};
	PartitionView view{parent, 100, 400};
	EXPECT_EQ(view.sizeInBytes(), 400U);
	EXPECT_EQ(view.sectorSize(), kSector);
}

TEST(PartitionView, FullReadInsideWindow) {
	const auto parentData = makeBytes(1024);
	InMemoryDevice parent{parentData, kSector};
	PartitionView view{parent, 100, 400};
	std::vector<std::byte> buffer(400);
	const auto result = view.readAt(0, buffer);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 400U);
	const auto full = std::span{parentData};
	const auto expected = full.subspan(100, 400);
	ASSERT_TRUE(std::ranges::equal(buffer, expected));
}

TEST(PartitionView, PartialReadAtWindowTail) {
	auto parent = InMemoryDevice{makeBytes(1024), kSector};
	PartitionView view{parent, 600, 400};
	std::vector<std::byte> buffer(100);
	const auto result = view.readAt(350, buffer);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 50U);
	EXPECT_EQ(buffer.at(0), static_cast<std::byte>(static_cast<std::uint8_t>((600 + 350) & 0xFFU)));
}

TEST(PartitionView, ReadPastWindowEndIsShort) {
	auto parent = InMemoryDevice{makeBytes(1024), kSector};
	PartitionView view{parent, 100, 400};
	std::vector<std::byte> buffer(100);
	const auto result = view.readAt(400, buffer);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 0U);
}

TEST(PartitionView, StartBeyondParentIsZeroLength) {
	auto parent = InMemoryDevice{makeBytes(1024), kSector};
	PartitionView view{parent, 2000, 100};
	EXPECT_EQ(view.sizeInBytes(), 0U);
	std::vector<std::byte> buffer(100);
	EXPECT_EQ(view.readAt(0, buffer).value(), 0U);
}

TEST(PartitionView, ZeroLengthViewReadsNothing) {
	auto parent = InMemoryDevice{makeBytes(1024), kSector};
	PartitionView view{parent, 100, 0};
	EXPECT_EQ(view.sizeInBytes(), 0U);
	std::vector<std::byte> buffer(100);
	EXPECT_EQ(view.readAt(0, buffer).value(), 0U);
}

TEST(PartitionView, OverflowingOffsetIsTypedError) {
	auto parent = InMemoryDevice{makeBytes(1024), kSector};
	PartitionView view{parent, 1, 100};
	std::vector<std::byte> buffer(1);
	const auto result = view.readAt(std::numeric_limits<std::uint64_t>::max(), buffer);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOverflow);
}

TEST(PartitionView, ParentBytesOutsideWindowAreUnreachable) {
	const auto parentData = makeBytes(256);
	InMemoryDevice parent{parentData, kSector};
	PartitionView view{parent, 50, 100};
	std::vector<std::byte> buffer(200, std::byte{0xFF});
	const auto result = view.readAt(0, buffer);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 100U);
	const std::span tail = std::span{buffer}.subspan(100, 100);
	ASSERT_TRUE(std::ranges::all_of(tail, [](std::byte b) { return b == std::byte{0xFF}; }));
}

} // namespace
