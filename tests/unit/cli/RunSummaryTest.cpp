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
				.filesystemMounted = true,
				.nonConformingVolume = false,
				.scanComplete = true},
		.winners = 5,
		.suppressed = 2,
		.extraction =
			ExtractionStats{
				.filesWritten = 5,
				.bytesWritten = 4096,
				.failed = 0,
				.renamed = 1,
				.deduplicated = 2,
				.degraded = 0},
		.delivery = revenant::cli::Delivery::kExtract,
		.unreadableBytes = 0};
}

// The same run over a device that would not give up one of its sectors.
[[nodiscard]] RunReport damagedRun() {
	RunReport report = hybridRun();
	report.unreadableBytes = 512;
	report.extraction.degraded = 1;
	return report;
}

// The same run stopped before extraction: everything was named, nothing was
// written, and the summary must not read as though something was.
[[nodiscard]] RunReport previewRun() {
	RunReport report = hybridRun();
	report.delivery = revenant::cli::Delivery::kPreview;
	report.extraction = ExtractionStats{
		.filesWritten = 0,
		.bytesWritten = 0,
		.failed = 1,
		.renamed = 1,
		.deduplicated = 0,
		.degraded = 0};
	return report;
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
		"extraction: files 5, bytes 4096, failed 0, renamed 1, deduplicated 2"};
	EXPECT_EQ(summarize(hybridRun()), expected);
}

// story-0604: a run that zero-filled anything cannot end looking like one that
// did not, so it gains a line an undamaged run does not have.
TEST(RunSummary, ADamagedRunSaysWhatItInvented) {
	const auto lines = summarize(damagedRun());
	ASSERT_EQ(lines.size(), 4U);
	EXPECT_EQ(
		lines.back(),
		"damage: unreadable bytes 512, artifacts with invented bytes 1"
		" (unreadable sectors were written as zeros; see `invented` in the manifest)");
}

TEST(RunSummary, AnUndamagedRunSaysNothingAboutDamage) {
	EXPECT_EQ(summarize(hybridRun()).size(), 3U);
}

// A volume that would not mount downgrades a hybrid run to carving rather than
// ending it, so the run has to say that is what happened.
TEST(RunSummary, StatesThatThereWasNoFilesystemToRead) {
	EXPECT_EQ(
		summarize(unmountableRun()).front(),
		"discovery: filesystem entries 0, carve candidates 3, regions scanned 2"
		" (no readable filesystem; carved the whole device)");
}

// A volume below FAT32's own cluster minimum was still read, because refusing
// it would throw away files that are plainly there. Warnings are what that is
// for, so the discovery line carries one.
TEST(RunSummary, WarnsWhenTheVolumeIsNotWhatAConformingFormatterWrites) {
	auto report = hybridRun();
	report.discovery.nonConformingVolume = true;
	EXPECT_EQ(
		summarize(report).front(),
		"discovery: filesystem entries 4, carve candidates 3, regions scanned 2"
		" (warning: the volume's metadata is not what a conforming formatter writes;"
		" recovery is best-effort)");
}

TEST(RunSummary, APreviewReadsAsAPreviewRatherThanAnEmptyExtraction) {
	EXPECT_EQ(
		summarize(previewRun()).back(),
		"preview: artifacts 4, unusable 1, renamed 1 (nothing was written)");
}

// The refusal an operator meets when they point the tool at a real disk without
// the privilege to read it — and the only sentence in this file that something
// outside the tree asserts verbatim: `tools/loopdev/`'s manual pass matches it
// character for character against what a genuinely refused open prints
// (story-0603). Nothing gates that copy, so the text is pinned here; reword it
// and this fails rather than the harness rotting until the next manual run.
TEST(RunSummary, SpellsThePrivilegeRefusalExactly) {
	EXPECT_EQ(
		describe(Error{.code = ErrorCode::kPermissionDenied, .offset = 0, .osCode = 0}),
		"the operating system refused to open the source: reading a whole disk or a mounted"
		" volume needs administrator (Windows) or root/disk-group (Linux) privilege");
}

TEST(RunSummary, EveryFailureDescribesItselfInWords) {
	for (const auto code :
		 {ErrorCode::kOutOfRange,
		  ErrorCode::kOverflow,
		  ErrorCode::kInvalidArgument,
		  ErrorCode::kNotFound,
		  ErrorCode::kIoFailure,
		  ErrorCode::kPermissionDenied,
		  ErrorCode::kNotBlockAddressable,
		  ErrorCode::kDestinationOnSource}) {
		EXPECT_FALSE(describe(Error{.code = code, .offset = 0, .osCode = 0}).empty());
	}
}

// The refusal an operator is likeliest to meet with a real disk, and the one
// whose sentence has to carry them to a different disk rather than to a
// different spelling (story-0609).
TEST(RunSummary, TellsADestinationOnTheSourceToMoveToAnotherDisk) {
	const auto said =
		describe(Error{.code = ErrorCode::kDestinationOnSource, .offset = 0, .osCode = 0});
	EXPECT_NE(said.find("overwrite"), std::string::npos);
	EXPECT_NE(said.find("different physical disk"), std::string::npos);
}

// One code, one failure. `kInvalidArgument` served the destination rule and a
// name nothing safe survived at the same time; the rule has its own code now,
// so the older sentence must stop claiming it.
TEST(RunSummary, LeavesTheStorageRuleOutOfTheInvalidArgumentSentence) {
	const auto said =
		describe(Error{.code = ErrorCode::kInvalidArgument, .offset = 0, .osCode = 0});
	EXPECT_EQ(said.find("storage"), std::string::npos);
	EXPECT_NE(
		said,
		describe(Error{.code = ErrorCode::kDestinationOnSource, .offset = 0, .osCode = 0}));
}

// The sentence has to name the fix, not the failure: someone who pointed this at
// a share has a real disk somewhere behind it (ADR-0007, story-0406).
TEST(RunSummary, TellsAFolderSourceWhatToPointAtInstead) {
	const auto said =
		describe(Error{.code = ErrorCode::kNotBlockAddressable, .offset = 0, .osCode = 0});
	EXPECT_NE(said.find("disk image or a device"), std::string::npos);
	EXPECT_NE(said.find("folder"), std::string::npos);
}

// Privilege is the likeliest first failure against a real disk, and "not found"
// would send an operator looking in entirely the wrong place (story-0401).
TEST(RunSummary, TellsAPrivilegeFailureFromAMissingOne) {
	const auto refused =
		describe(Error{.code = ErrorCode::kPermissionDenied, .offset = 0, .osCode = 0});
	EXPECT_NE(refused, describe(Error{.code = ErrorCode::kNotFound, .offset = 0, .osCode = 0}));
	EXPECT_NE(refused.find("administrator"), std::string::npos);
}

TEST(RunSummary, DistinguishesAMissingPathFromARefusedOne) {
	const auto missing = describe(Error{.code = ErrorCode::kNotFound, .offset = 0, .osCode = 0});
	const auto refused =
		describe(Error{.code = ErrorCode::kInvalidArgument, .offset = 0, .osCode = 0});
	EXPECT_NE(missing, refused);
}

} // namespace
