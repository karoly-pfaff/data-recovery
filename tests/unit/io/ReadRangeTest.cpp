// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/ReadRange.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "revenant/core/Error.hpp"

namespace {

using revenant::clampReadRange;
using revenant::ErrorCode;

constexpr std::uint64_t kDeviceSize = 1024;

TEST(ReadRange, OverflowingOffsetPlusBufferIsTypedError) {
    const auto offset = std::numeric_limits<std::uint64_t>::max() - 3;
    const auto result = clampReadRange(offset, 10, kDeviceSize);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, ErrorCode::kOverflow);
    EXPECT_EQ(result.error().offset, offset);
}

TEST(ReadRange, OffsetAtOrPastDeviceEndClampsToZero) {
    EXPECT_EQ(clampReadRange(kDeviceSize, 10, kDeviceSize).value(), 0U);
    EXPECT_EQ(clampReadRange(kDeviceSize + 1000, 10, kDeviceSize).value(), 0U);
}

TEST(ReadRange, EmptyBufferClampsToZero) {
    EXPECT_EQ(clampReadRange(0, 0, kDeviceSize).value(), 0U);
}

TEST(ReadRange, RequestWithinBoundsIsUnclamped) {
    EXPECT_EQ(clampReadRange(0, 100, kDeviceSize).value(), 100U);
}

TEST(ReadRange, RequestCrossingDeviceEndClampsToRemainder) {
    EXPECT_EQ(clampReadRange(kDeviceSize - 40, 100, kDeviceSize).value(), 40U);
}

} // namespace
