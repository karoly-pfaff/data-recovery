// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/log/Logger.hpp"

#include <gtest/gtest.h>

#include "revenant/core/log/LogLevel.hpp"
#include "support/CapturingLogSink.hpp"

namespace {

using revenant::Logger;
using revenant::LogLevel;
using revenant::testing::CapturingLogSink;

TEST(Logger, FiltersBelowMinimumLevel) {
	CapturingLogSink sink;
	Logger logger{sink, LogLevel::kWarn};
	logger.log(LogLevel::kInfo, "dropped");
	EXPECT_TRUE(sink.records().empty());
}

TEST(Logger, ForwardsAtAndAboveMinimumLevel) {
	CapturingLogSink sink;
	Logger logger{sink, LogLevel::kWarn};
	logger.log(LogLevel::kWarn, "kept");
	logger.log(LogLevel::kError, "also kept");
	ASSERT_EQ(sink.records().size(), 2U);
	EXPECT_EQ(sink.records().at(0).message, "kept");
	EXPECT_EQ(sink.records().at(1).level, LogLevel::kError);
}

TEST(LogLevel, EveryLevelHasAName) {
	EXPECT_EQ(revenant::toString(LogLevel::kTrace), "trace");
	EXPECT_EQ(revenant::toString(LogLevel::kDebug), "debug");
	EXPECT_EQ(revenant::toString(LogLevel::kInfo), "info");
	EXPECT_EQ(revenant::toString(LogLevel::kWarn), "warn");
	EXPECT_EQ(revenant::toString(LogLevel::kError), "error");
}

} // namespace
