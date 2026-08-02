// SPDX-License-Identifier: GPL-3.0-or-later
// An interrupted recovery, resumed. A run stopped mid-scan must leave a session
// the next run can carry on from, and carrying on must land exactly where an
// uninterrupted run would — that is the whole promise of ADR-0008.
#include <gtest/gtest.h>

#include <algorithm>
#include <csignal>
#include <filesystem>
#include <string>
#include <vector>

#include "cli/Interrupt.hpp"
#include "cli/RecoveryOptions.hpp"
#include "cli/RecoveryRun.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/Checkpoint.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/Manifest.hpp"
#include "support/FixtureContent.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::cli::catchInterrupts;
using revenant::cli::Delivery;
using revenant::cli::kSessionDirectoryName;
using revenant::cli::runRecovery;
using revenant::cli::RunRequest;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::recovery::kManifestFileName;
using revenant::recovery::readCheckpoint;
using revenant::recovery::RecoveryMode;
using revenant::testing::fixtureContentNamed;
using revenant::testing::readFileBytes;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

// Every recovered file, as destination-relative paths in a stable order — the
// session directory excluded, since it is state rather than output.
[[nodiscard]] std::vector<std::string> recoveredFiles(const std::filesystem::path& destination) {
	std::vector<std::string> names;
	for (const auto& entry : std::filesystem::recursive_directory_iterator{destination}) {
		const auto relative = std::filesystem::relative(entry.path(), destination);
		if (entry.is_regular_file() && *relative.begin() != kSessionDirectoryName) {
			names.push_back(relative.generic_string());
		}
	}
	std::ranges::sort(names);
	return names;
}

// One image and one destination, and the request that recovers the first into
// the second.
class Recovery {
public:
	Recovery() : image_(buildNtfsImage()) {}

	[[nodiscard]] RunRequest request() const {
		return RunRequest{
			.source = image_.path(),
			.destination = output_.path(),
			.session = output_.path() / std::filesystem::path{kSessionDirectoryName},
			.mode = RecoveryMode::kHybrid,
			.delivery = Delivery::kExtract,
			.formats = {}};
	}

	[[nodiscard]] const std::filesystem::path& destination() const noexcept {
		return output_.path();
	}

private:
	TempFile image_;
	TempDir output_;
};

// Ctrl-C between chunks: the run stops after the one it is on.
[[nodiscard]] revenant::Result<revenant::cli::RunReport> interruptedRun(const RunRequest& request) {
	catchInterrupts();
	static_cast<void>(std::raise(SIGINT));
	return runRecovery(request);
}

[[nodiscard]] revenant::Result<revenant::cli::RunReport>
uninterruptedRun(const RunRequest& request) {
	catchInterrupts();
	return runRecovery(request);
}

TEST(ResumedRecovery, AnInterruptedRunDecidesNothingAndWritesNoFiles) {
	const Recovery recovery;
	const auto stopped = interruptedRun(recovery.request());
	ASSERT_TRUE(stopped.hasValue());
	EXPECT_FALSE(stopped.value().discovery.scanComplete);
	EXPECT_TRUE(recoveredFiles(recovery.destination()).empty());
}

// It does leave a manifest, which it did not before story-0605. Deciding
// nothing is still right — arbitrating a partial index can crown a winner the
// finished scan would have suppressed — but saying nothing was not: a run that
// stopped has to record that it stopped, and where.
TEST(ResumedRecovery, AnInterruptedRunRecordsThatItStopped) {
	const Recovery recovery;
	ASSERT_TRUE(interruptedRun(recovery.request()).hasValue());
	const auto manifest =
		recovery.request().session / std::filesystem::path{std::string{kManifestFileName}};
	ASSERT_TRUE(std::filesystem::exists(manifest));
	const auto text = revenant::testing::readFileText(manifest);
	EXPECT_NE(text.find(R"("outcome":"stopped-resumable")"), std::string::npos);
	EXPECT_NE(text.find(R"("artifacts":[])"), std::string::npos);
}

TEST(ResumedRecovery, AnInterruptedRunLeavesTheCursorItGotTo) {
	const Recovery recovery;
	ASSERT_TRUE(interruptedRun(recovery.request()).hasValue());
	const auto checkpoint = readCheckpoint(recovery.request().session);
	ASSERT_TRUE(checkpoint.hasValue());
	EXPECT_GT(checkpoint.value().scanCursor, 0U);
	EXPECT_GT(checkpoint.value().indexRecords, 0U);
}

// The sentence the story exists to be able to say.
TEST(ResumedRecovery, CarryingOnLandsWhereAnUninterruptedRunWould) {
	const Recovery whole;
	ASSERT_TRUE(uninterruptedRun(whole.request()).hasValue());

	const Recovery resumed;
	ASSERT_FALSE(interruptedRun(resumed.request()).value().discovery.scanComplete);
	const auto finished = uninterruptedRun(resumed.request());
	ASSERT_TRUE(finished.hasValue());
	EXPECT_TRUE(finished.value().discovery.scanComplete);
	EXPECT_EQ(recoveredFiles(resumed.destination()), recoveredFiles(whole.destination()));
}

TEST(ResumedRecovery, TheResumedRunsFilesAreTheRightBytes) {
	const Recovery recovery;
	ASSERT_TRUE(interruptedRun(recovery.request()).hasValue());
	ASSERT_TRUE(uninterruptedRun(recovery.request()).hasValue());
	EXPECT_EQ(
		readFileBytes(recovery.destination() / "photos" / "deleted.jpg"),
		fixtureContentNamed("deleted.jpg"));
}

// A second run over a finished session has nothing left to scan, so it decides
// and delivers from the index it already has — "scan now, extract later".
TEST(ResumedRecovery, RunningAgainAfterAFinishedScanStillDelivers) {
	const Recovery recovery;
	ASSERT_TRUE(uninterruptedRun(recovery.request()).hasValue());
	const auto again = uninterruptedRun(recovery.request());
	ASSERT_TRUE(again.hasValue());
	EXPECT_TRUE(again.value().discovery.scanComplete);
	EXPECT_GT(again.value().winners, 0U);
}

} // namespace
