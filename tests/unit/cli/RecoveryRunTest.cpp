// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RecoveryRun.hpp"

#include <gtest/gtest.h>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace {

using revenant::Error;
using revenant::ErrorCode;
using revenant::Result;
using revenant::cli::withoutLostRecords;
using revenant::recovery::RecoveryStats;

[[nodiscard]] Result<RecoveryStats> scanned() {
	return RecoveryStats{
		.entriesReported = 4,
		.candidatesReported = 3,
		.accountedBytes = 8192,
		.regionsScanned = 2,
		.regionsDropped = 0,
		.filesystemMounted = true};
}

TEST(RecoveryRun, LetsThroughARunThatRecordedEverythingItFound) {
	const auto stats = withoutLostRecords(scanned(), 0);
	ASSERT_TRUE(stats.hasValue());
	EXPECT_EQ(stats.value().entriesReported, 4U);
}

// Every count after this point is read back out of the index, so one record
// that never reached it makes the whole answer wrong rather than smaller.
TEST(RecoveryRun, FailsARunThatCouldNotWriteDownWhatItFound) {
	const auto stats = withoutLostRecords(scanned(), 1);
	ASSERT_FALSE(stats.hasValue());
	EXPECT_EQ(stats.error().code, ErrorCode::kIoFailure);
}

TEST(RecoveryRun, ForwardsAScanThatFailedOnItsOwn) {
	const Error fault{.code = ErrorCode::kOutOfRange, .offset = 512, .osCode = 0};
	EXPECT_EQ(withoutLostRecords(fault, 0).error(), fault);
}

// The read fault is what an operator can act on; the records it cost are a
// consequence, so the original failure is the one that survives.
TEST(RecoveryRun, KeepsTheFirstFailureRatherThanTheOneItCaused) {
	const Error fault{.code = ErrorCode::kIoFailure, .offset = 4096, .osCode = 5};
	EXPECT_EQ(withoutLostRecords(fault, 7).error(), fault);
}

} // namespace
