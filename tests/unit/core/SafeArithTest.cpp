// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SafeArith.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "revenant/core/Error.hpp"

namespace {

using revenant::ErrorCode;
using revenant::safeAdd64;
using revenant::saturatingAdd64;
using revenant::safeMul32;
using revenant::safeMul64;

constexpr auto kMax32 = std::numeric_limits<std::uint32_t>::max();
constexpr auto kMax64 = std::numeric_limits<std::uint64_t>::max();

// The diagnostic offset is a field's byte position, reported on the error and
// never an operand — so every rejection below asserts it survives the failure.
constexpr std::uint64_t kOffset = 0x0D;

TEST(SafeMul32, ProductInsideTheTypeIsReturned) {
	const auto result = safeMul32(512, 8, kOffset);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 4096U);
}

TEST(SafeMul32, ProductAtTheTypeMaximumIsReturned) {
	const auto result = safeMul32(kMax32, 1, kOffset);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), kMax32);
}

// 0x10000 * 0x10000 is 2^32 — exactly one above the maximum, so the guard is
// pinned from the reject side too. A product merely far above the limit would
// leave a guard widened by one alive; this case loses to it. (Widening it has
// to be written with a 64-bit right-hand side — `max() + 1` in this file's own
// types wraps to 0 and rejects everything instead.)
TEST(SafeMul32, ProductOneAboveTheTypeMaximumIsRejected) {
	const auto result = safeMul32(0x10000, 0x10000, kOffset);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOverflow);
	EXPECT_EQ(result.error().offset, kOffset);
}

TEST(SafeMul32, ZeroOperandProducesZero) {
	const auto result = safeMul32(0, kMax32, kOffset);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 0U);
}

TEST(SafeMul64, ProductInsideTheTypeIsReturned) {
	const auto result = safeMul64(1U << 20U, 4096, kOffset);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 4294967296ULL);
}

// The guard is `b > max / a`, so the largest accepted `b` is exactly `max / a`.
// Without this case, relaxing the guard to `>=` passes every other test in the
// tree — the reject-side probes below and in RunlistExtentsTest cannot see it.
TEST(SafeMul64, ProductAtTheLargestAcceptedOperandIsReturned) {
	const auto result = safeMul64(3, kMax64 / 3, kOffset);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 3 * (kMax64 / 3));
}

// One step past the same boundary: for a = 2^63 the largest accepted b is 1,
// so b = 2 is the first refusal. Keep the operands adjacent to the limit — a
// product merely far above it would stop pinning the guard.
TEST(SafeMul64, ProductThatWouldWrapIsRejected) {
	const auto result = safeMul64((kMax64 / 2) + 1, 2, kOffset);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOverflow);
	EXPECT_EQ(result.error().offset, kOffset);
}

// The zero guard is what keeps the division in the overflow check defined.
TEST(SafeMul64, ZeroOperandNeverOverflows) {
	const auto result = safeMul64(0, kMax64, kOffset);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), 0ULL);
}

// The guard is `b > max - a`, so `b == max - a` is the largest accepted addend.
// This case sits on it, and so does AddingZeroToTheMaximum below — from the far
// end, where both sides of the comparison are 0. Relaxing the guard to `>=`
// loses them both, which is the redundancy an accept-side boundary is worth.
TEST(SafeAdd64, SumAtTheLargestAcceptedAddendIsReturned) {
	const auto result = safeAdd64(kMax64 - 1, 1, kOffset);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), kMax64);
}

TEST(SafeAdd64, SumOneAboveTheTypeMaximumIsRejected) {
	const auto result = safeAdd64(kMax64, 1, kOffset);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOverflow);
	EXPECT_EQ(result.error().offset, kOffset);
}

// The boundary again, approached from the far end: `max - a` is 0 and so is `b`.
// It pins the same `>` by a different route than the case above.
TEST(SafeAdd64, AddingZeroToTheMaximumIsNotAnOverflow) {
	const auto result = safeAdd64(kMax64, 0, kOffset);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), kMax64);
}

// The other answer to an overflow, for callers with nowhere to put an error: a
// sum pinned at the maximum. What matters is that it never *wraps* — a range
// that wrapped to a small number would sit somewhere real and be compared
// against real things, which is the outcome the saturation exists to prevent.
TEST(SaturatingAdd64, AnOrdinarySumIsJustTheSum) {
	EXPECT_EQ(saturatingAdd64(1024, 512), 1536U);
}

TEST(SaturatingAdd64, ASumThatWouldOverflowIsPinnedAtTheMaximum) {
	EXPECT_EQ(saturatingAdd64(kMax64, 1), kMax64);
	EXPECT_EQ(saturatingAdd64(kMax64 - 1, 512), kMax64);
	EXPECT_EQ(saturatingAdd64(kMax64, kMax64), kMax64);
}

TEST(SaturatingAdd64, TheLargestSumThatStillFitsIsExact) {
	EXPECT_EQ(saturatingAdd64(kMax64 - 1, 1), kMax64);
}

} // namespace
