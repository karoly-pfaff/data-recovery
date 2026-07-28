// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RunSummary.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "cli/RecoveryRun.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace {

using revenant::Error;
using revenant::ErrorCode;
using revenant::cli::describe;
using revenant::cli::RunReport;
using revenant::cli::summarize;
using revenant::recovery::ExtractionStats;
using revenant::recovery::RecoveryStats;

[[nodiscard]] RunReport hybridRun() {
	return RunReport{
		.discovery =
			RecoveryStats{
				.entriesReported = 4,
				.candidatesReported = 3,
				.accountedBytes = 8192,
				.regionsScanned = 2,
				.regionsDropped = 0,
				.filesystemMounted = true},
		.winners = 5,
		.suppressed = 2,
		.extraction =
			ExtractionStats{.filesWritten = 5, .bytesWritten = 4096, .failed = 0, .renamed = 1}};
}

// The same run over a volume that would not mount: no entries, and the fact
// itself recorded.
[[nodiscard]] RunReport unmountableRun() {
	RunReport report = hybridRun();
	report.discovery.entriesReported = 0;
	report.discovery.filesystemMounted = false;
	return report;
}

TEST(RunSummary, SaysWhatWasFoundWhatWasChosenAndWhatWasWritten) {
	const std::vector<std::string> expected{
		"discovery: filesystem entries 4, carve candidates 3, regions scanned 2",
		"arbitration: winners 5, suppressed 2",
		"extraction: files 5, bytes 4096, failed 0, renamed 1"};
	EXPECT_EQ(summarize(hybridRun()), expected);
}

// A volume that would not mount downgrades a hybrid run to carving rather than
// ending it, so the run has to say that is what happened.
TEST(RunSummary, StatesThatThereWasNoFilesystemToRead) {
	EXPECT_EQ(
		summarize(unmountableRun()).front(),
		"discovery: filesystem entries 0, carve candidates 3, regions scanned 2"
		" (no readable filesystem; carved the whole device)");
}

TEST(RunSummary, EveryFailureDescribesItselfInWords) {
	for (const auto code :
		 {ErrorCode::kOutOfRange,
		  ErrorCode::kOverflow,
		  ErrorCode::kInvalidArgument,
		  ErrorCode::kNotFound,
		  ErrorCode::kIoFailure}) {
		EXPECT_FALSE(describe(Error{.code = code, .offset = 0, .osCode = 0}).empty());
	}
}

TEST(RunSummary, DistinguishesAMissingPathFromARefusedOne) {
	const auto missing = describe(Error{.code = ErrorCode::kNotFound, .offset = 0, .osCode = 0});
	const auto refused =
		describe(Error{.code = ErrorCode::kInvalidArgument, .offset = 0, .osCode = 0});
	EXPECT_NE(missing, refused);
}

} // namespace
