// SPDX-License-Identifier: GPL-3.0-or-later
// story-0050: the arithmetic the performance gate is written in terms of. A
// benchmark harness whose own statistics are unverified is a strange thing to
// gate merges on, which is why these are pure functions rather than a library's
// black box.
#include "perf/Statistics.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

using revenant::perf::ratePerSecond;
using revenant::perf::Statistics;
using revenant::perf::statisticsOf;

TEST(Statistics, TakesTheMiddleOfAnOddNumberOfSamples) {
	const auto stats = statisticsOf({0.3, 0.1, 0.2});
	EXPECT_DOUBLE_EQ(stats.medianSeconds, 0.2);
}

TEST(Statistics, AveragesTheMiddleTwoOfAnEvenNumber) {
	const auto stats = statisticsOf({0.4, 0.1, 0.2, 0.3});
	EXPECT_DOUBLE_EQ(stats.medianSeconds, 0.25);
}

TEST(Statistics, ReportsTheExtremesWhateverOrderTheyArrivedIn) {
	const auto stats = statisticsOf({0.3, 0.1, 0.2});
	EXPECT_DOUBLE_EQ(stats.minSeconds, 0.1);
	EXPECT_DOUBLE_EQ(stats.maxSeconds, 0.3);
}

// Relative, because a 2 ms swing means something very different on a 5 ms
// benchmark than on a 5 s one.
TEST(Statistics, MeasuresSpreadAgainstTheMedian) {
	const auto stats = statisticsOf({0.1, 0.2, 0.3});
	EXPECT_DOUBLE_EQ(stats.spread, 1.0);
}

TEST(Statistics, OneSampleDisagreesWithNothing) {
	const auto stats = statisticsOf({0.25});
	EXPECT_DOUBLE_EQ(stats.medianSeconds, 0.25);
	EXPECT_DOUBLE_EQ(stats.spread, 0.0);
}

// A benchmark that ran nothing has no timing, and that is a number the report
// can print rather than undefined behaviour.
TEST(Statistics, NoSamplesIsAZeroStatistic) {
	const auto stats = statisticsOf({});
	EXPECT_DOUBLE_EQ(stats.medianSeconds, 0.0);
	EXPECT_DOUBLE_EQ(stats.minSeconds, 0.0);
	EXPECT_DOUBLE_EQ(stats.spread, 0.0);
}

TEST(Statistics, DividesWorkByTheMedianToGetARate) {
	const auto stats = statisticsOf({0.5});
	EXPECT_DOUBLE_EQ(ratePerSecond(32.0, stats), 64.0);
}

// A rate over no time is not infinity; it is unmeasured.
TEST(Statistics, ARateOverNoTimeIsZero) {
	EXPECT_DOUBLE_EQ(ratePerSecond(32.0, Statistics{}), 0.0);
}

} // namespace
