// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/BoundedCount.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "revenant/core/Error.hpp"

namespace {

using revenant::boundedCount;
using revenant::ErrorCode;

TEST(BoundedCount, UnderBoundReturnsValue) {
	const auto result = boundedCount<std::uint32_t>(3, 10);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 3U);
}

TEST(BoundedCount, AtBoundReturnsValue) {
	const auto result = boundedCount<std::uint32_t>(10, 10);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 10U);
}

TEST(BoundedCount, OverBoundIsTypedError) {
	const auto result = boundedCount<std::uint32_t>(11, 10);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOutOfRange);
}

TEST(BoundedCount, ZeroBoundRejectsAnyPositiveCount) {
	EXPECT_FALSE(boundedCount<std::uint32_t>(1, 0).hasValue());
}

TEST(BoundedCount, ZeroBoundAcceptsZeroCount) {
	const auto result = boundedCount<std::uint32_t>(0, 0);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 0U);
}

TEST(BoundedCount, WideUntrustedTypeIsComparedSafely) {
	const auto result = boundedCount<std::uint64_t>(0xFFFFFFFFFFULL, 10);
	EXPECT_FALSE(result.hasValue());
}

} // namespace
