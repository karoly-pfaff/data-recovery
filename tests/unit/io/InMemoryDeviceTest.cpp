// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/InMemoryDevice.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace {

using revenant::BlockDevice;
using revenant::ErrorCode;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kTestSectorSize = 512;

std::vector<std::byte> patternBytes(std::size_t count) {
	std::vector<std::byte> bytes(count);
	for (std::size_t i = 0; i < count; ++i) {
		bytes.at(i) = static_cast<std::byte>(i & 0xFFU);
	}
	return bytes;
}

// The seam test: consumers only ever see BlockDevice&. offset/count mirror
// BlockDevice::readAt's own parameter order; not worth a wrapper type here.
std::vector<std::byte>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
readThroughInterface(BlockDevice& device, std::uint64_t offset, std::size_t count) {
	std::vector<std::byte> buffer(count);
	const auto got = device.readAt(offset, buffer);
	buffer.resize(got.value());
	return buffer;
}

TEST(InMemoryDevice, FullReadThroughInterface) {
	InMemoryDevice device{patternBytes(64), kTestSectorSize};
	const auto bytes = readThroughInterface(device, 0, 64);
	ASSERT_EQ(bytes.size(), 64U);
	EXPECT_EQ(bytes.at(63), std::byte{63});
}

TEST(InMemoryDevice, TailReadIsShort) {
	InMemoryDevice device{patternBytes(64), kTestSectorSize};
	EXPECT_EQ(readThroughInterface(device, 60, 16).size(), 4U);
}

TEST(InMemoryDevice, ReadPastEndIsZeroLengthValue) {
	InMemoryDevice device{patternBytes(64), kTestSectorSize};
	EXPECT_EQ(readThroughInterface(device, 64, 8).size(), 0U);
	EXPECT_EQ(readThroughInterface(device, 1000, 8).size(), 0U);
}

TEST(InMemoryDevice, ZeroLengthReadReadsNothing) {
	InMemoryDevice device{patternBytes(64), kTestSectorSize};
	EXPECT_EQ(readThroughInterface(device, 0, 0).size(), 0U);
}

TEST(InMemoryDevice, OverflowingRangeIsTypedError) {
	InMemoryDevice device{patternBytes(64), kTestSectorSize};
	std::vector<std::byte> buffer(4);
	const auto result = device.readAt(std::numeric_limits<std::uint64_t>::max() - 1, buffer);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOverflow);
}

TEST(InMemoryDevice, ReportsConstructedGeometry) {
	InMemoryDevice device{patternBytes(8192), 4096};
	EXPECT_EQ(device.sizeInBytes(), 8192U);
	EXPECT_EQ(device.sectorSize(), 4096U);
}

} // namespace
