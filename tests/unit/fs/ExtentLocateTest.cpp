// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ExtentLocate.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/fs/Types.hpp"

namespace {

using revenant::ErrorCode;
using revenant::fs::Extent;
using revenant::fs::FileRange;
using revenant::fs::locateInExtents;

constexpr std::uint64_t kMaxOffset = std::numeric_limits<std::uint64_t>::max();

// Two runs whose device order is the reverse of their file order — which is
// what fragmentation looks like, and why a file offset needs a lookup.
[[nodiscard]] std::vector<Extent> fragmented() {
	return {
		Extent{.deviceOffset = 8000, .lengthBytes = 100},
		Extent{.deviceOffset = 2000, .lengthBytes = 50}};
}

TEST(ExtentLocate, MapsTheFirstByteOfTheFirstExtent) {
	const auto located = locateInExtents(fragmented(), FileRange{.offset = 0, .length = 10});
	ASSERT_TRUE(located.hasValue());
	EXPECT_EQ(located.value(), 8000U);
}

TEST(ExtentLocate, MapsTheLastByteOfTheFirstExtent) {
	const auto located = locateInExtents(fragmented(), FileRange{.offset = 99, .length = 1});
	ASSERT_TRUE(located.hasValue());
	EXPECT_EQ(located.value(), 8099U);
}

TEST(ExtentLocate, MapsAcrossTheSeamIntoTheSecondExtent) {
	const auto located = locateInExtents(fragmented(), FileRange{.offset = 100, .length = 10});
	ASSERT_TRUE(located.hasValue());
	EXPECT_EQ(located.value(), 2000U);
}

TEST(ExtentLocate, MapsTheLastByteOfTheLastExtent) {
	const auto located = locateInExtents(fragmented(), FileRange{.offset = 149, .length = 1});
	ASSERT_TRUE(located.hasValue());
	EXPECT_EQ(located.value(), 2049U);
}

TEST(ExtentLocate, SkipsAZeroLengthExtent) {
	const std::vector<Extent> extents{
		Extent{.deviceOffset = 4096, .lengthBytes = 0},
		Extent{.deviceOffset = 512, .lengthBytes = 16}};
	const auto located = locateInExtents(extents, FileRange{.offset = 0, .length = 16});
	ASSERT_TRUE(located.hasValue());
	EXPECT_EQ(located.value(), 512U);
}

TEST(ExtentLocate, RejectsAnOffsetPastTheLastExtent) {
	const auto located = locateInExtents(fragmented(), FileRange{.offset = 150, .length = 1});
	ASSERT_FALSE(located.hasValue());
	EXPECT_EQ(located.error().code, ErrorCode::kOutOfRange);
}

TEST(ExtentLocate, RejectsEveryOffsetWhenThereAreNoExtents) {
	const auto located = locateInExtents({}, FileRange{.offset = 0, .length = 1});
	ASSERT_FALSE(located.hasValue());
	EXPECT_EQ(located.error().code, ErrorCode::kOutOfRange);
}

// Two runs are two device reads. Answering with the first extent's offset would
// silently hand back bytes that belong to whatever follows that run on disk.
TEST(ExtentLocate, RejectsARangeStraddlingTwoExtents) {
	const auto located = locateInExtents(fragmented(), FileRange{.offset = 95, .length = 10});
	ASSERT_FALSE(located.hasValue());
	EXPECT_EQ(located.error().code, ErrorCode::kInvalidArgument);
}

TEST(ExtentLocate, RejectsARangeWhoseEndOverflows) {
	const auto located =
		locateInExtents(fragmented(), FileRange{.offset = kMaxOffset - 1, .length = 4});
	ASSERT_FALSE(located.hasValue());
	EXPECT_EQ(located.error().code, ErrorCode::kOverflow);
}

TEST(ExtentLocate, RejectsAnExtentWhoseDeviceOffsetWouldOverflow) {
	const std::vector<Extent> extents{Extent{.deviceOffset = kMaxOffset - 4, .lengthBytes = 32}};
	const auto located = locateInExtents(extents, FileRange{.offset = 8, .length = 1});
	ASSERT_FALSE(located.hasValue());
	EXPECT_EQ(located.error().code, ErrorCode::kOverflow);
}

} // namespace
