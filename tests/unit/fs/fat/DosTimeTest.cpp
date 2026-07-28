// SPDX-License-Identifier: GPL-3.0-or-later
// story-0030: DOS date/time into the layer's one epoch. The two positive cases
// are checked against tick counts computed independently of the code under
// test; the rest pin down that an unreadable field yields no timestamp rather
// than a plausible-looking wrong one.
#include "fs/fat/DosTime.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using revenant::fs::fat::DosTimestamp;
using revenant::fs::fat::toFiletime;

// 1980-01-01 00:00:00 UTC — the FAT epoch, and the smallest date DOS can
// state. 3652 days after the Unix epoch, itself 11 644 473 600 s after
// FILETIME's.
constexpr std::uint16_t kEpochDate = (0U << 9U) | (1U << 5U) | 1U;
constexpr std::uint64_t kEpochTicks = 119'600'064'000'000'000ULL;

// 2020-02-29 12:34:56 UTC — a leap day, so the date arithmetic is exercised
// rather than assumed.
constexpr std::uint16_t kLeapDate = (40U << 9U) | (2U << 5U) | 29U;
constexpr std::uint16_t kLeapTime = (12U << 11U) | (34U << 5U) | 28U;
constexpr std::uint64_t kLeapTicks = 132'274'532'960'000'000ULL;

[[nodiscard]] std::uint64_t ticksOf(std::uint16_t date, std::uint16_t time) {
	return toFiletime(DosTimestamp{.date = date, .time = time});
}

TEST(FatDosTime, TheFatEpochIsItsKnownFiletime) {
	EXPECT_EQ(ticksOf(kEpochDate, 0), kEpochTicks);
}

TEST(FatDosTime, ALeapDayConvertsToItsKnownFiletime) {
	EXPECT_EQ(ticksOf(kLeapDate, kLeapTime), kLeapTicks);
}

// The two-second granularity is the format's, not a rounding choice: the field
// counts pairs of seconds and nothing finer was ever stored.
TEST(FatDosTime, TheSecondsFieldCountsPairsOfSeconds) {
	EXPECT_EQ(ticksOf(kEpochDate, 1U), kEpochTicks + (2ULL * 10'000'000ULL));
}

// An all-zero pair is how FAT says "never set". Month zero is not a month, so
// it falls out of the same rule that rejects damage.
TEST(FatDosTime, AnUnsetStampIsNoTimestamp) {
	EXPECT_EQ(ticksOf(0, 0), 0U);
}

TEST(FatDosTime, AMonthOutsideTheYearIsNoTimestamp) {
	EXPECT_EQ(ticksOf((0U << 9U) | (13U << 5U) | 1U, 0), 0U);
}

TEST(FatDosTime, ADayOutsideTheMonthIsNoTimestamp) {
	EXPECT_EQ(ticksOf((0U << 9U) | (1U << 5U) | 0U, 0), 0U);
}

TEST(FatDosTime, AnHourOutsideTheDayIsNoTimestamp) {
	EXPECT_EQ(ticksOf(kEpochDate, 24U << 11U), 0U);
}

TEST(FatDosTime, AMinuteOutsideTheHourIsNoTimestamp) {
	EXPECT_EQ(ticksOf(kEpochDate, 60U << 5U), 0U);
}

// The field can hold 30, which would mean 60 seconds. No clock says that.
TEST(FatDosTime, ASecondOutsideTheMinuteIsNoTimestamp) {
	EXPECT_EQ(ticksOf(kEpochDate, 30U), 0U);
}

} // namespace
