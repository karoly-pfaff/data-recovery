// SPDX-License-Identifier: GPL-3.0-or-later
// story-0605: the exit status a run leaves behind. One case per row of the
// table in README.md, because the numbers freeze at 1.0 (docs/versioning.md)
// and a table nothing checks is a promise nothing keeps.
#include "cli/RunOutcome.hpp"

#include <gtest/gtest.h>

#include "revenant/core/Error.hpp"

namespace {

using revenant::ErrorCode;
using revenant::cli::outcomeOf;
using revenant::cli::RunOutcome;

// The numbers themselves, asserted once. Everything below names the outcome;
// this is the only place that says which integer it is.
TEST(RunOutcome, TheCodesAreTheOnesTheTableDocuments) {
	EXPECT_EQ(static_cast<int>(RunOutcome::kFinished), 0);
	EXPECT_EQ(static_cast<int>(RunOutcome::kCouldNotStart), 1);
	EXPECT_EQ(static_cast<int>(RunOutcome::kUsageError), 2);
	EXPECT_EQ(static_cast<int>(RunOutcome::kStoppedResumable), 3);
	EXPECT_EQ(static_cast<int>(RunOutcome::kStoppedNeedsAttention), 4);
}

// Everything answered by changing an argument produced nothing at all.
TEST(RunOutcome, AFailureAboutWhatTheRunWasPointedAtCouldNotStart) {
	EXPECT_EQ(outcomeOf(ErrorCode::kNotFound), RunOutcome::kCouldNotStart);
	EXPECT_EQ(outcomeOf(ErrorCode::kInvalidArgument), RunOutcome::kCouldNotStart);
	EXPECT_EQ(outcomeOf(ErrorCode::kDestinationOnSource), RunOutcome::kCouldNotStart);
	EXPECT_EQ(outcomeOf(ErrorCode::kNotBlockAddressable), RunOutcome::kCouldNotStart);
	EXPECT_EQ(outcomeOf(ErrorCode::kPermissionDenied), RunOutcome::kCouldNotStart);
}

// A device that went away leaves a checkpoint and an index; the same command
// picks them up. There is nothing for the operator to fix first.
TEST(RunOutcome, ALostSourceIsResumable) {
	EXPECT_EQ(outcomeOf(ErrorCode::kSourceLost), RunOutcome::kStoppedResumable);
}

// Room is what is missing, and re-running before it is found fails the same way.
TEST(RunOutcome, ExhaustedStorageNeedsAttentionFirst) {
	EXPECT_EQ(outcomeOf(ErrorCode::kStorageExhausted), RunOutcome::kStoppedNeedsAttention);
}

// A fault the layers below could not classify stopped a run that had already
// started. It is not known to be resumable, so it asks to be looked at.
TEST(RunOutcome, AnUnclassifiedFaultAsksToBeLookedAt) {
	EXPECT_EQ(outcomeOf(ErrorCode::kIoFailure), RunOutcome::kStoppedNeedsAttention);
	EXPECT_EQ(outcomeOf(ErrorCode::kOutOfRange), RunOutcome::kStoppedNeedsAttention);
	EXPECT_EQ(outcomeOf(ErrorCode::kOverflow), RunOutcome::kStoppedNeedsAttention);
}

} // namespace
