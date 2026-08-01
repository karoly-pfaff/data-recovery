// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/HybridRecovery.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/ScanCandidate.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/RunScope.hpp"
#include "support/CollectingEntryVisitor.hpp"
#include "support/CollectingVisitor.hpp"
#include "support/NtfsVolume.hpp"
#include "support/RecordingProgress.hpp"
#include "support/WholeSourceScope.hpp"

namespace {

using revenant::ErrorCode;
using revenant::carve::CarverRegistry;
using revenant::carve::registerBuiltinCarvers;
using revenant::carve::ScanCandidate;
using revenant::carve::ScanConfig;
using revenant::carve::SignatureScanner;
using revenant::imagegen::ntfs::kUnallocatedJpegCluster;
using revenant::imagegen::ntfs::makeLayout;
using revenant::recovery::freshRun;
using revenant::recovery::HybridRecovery;
using revenant::recovery::kDefaultCheckpointBytes;
using revenant::recovery::RecoveryMode;
using revenant::recovery::RecoveryPlan;
using revenant::recovery::RecoveryStats;
using revenant::recovery::RunScope;
using revenant::testing::CollectingEntryVisitor;
using revenant::testing::CollectingVisitor;
using revenant::testing::NtfsVolume;
using revenant::testing::RecordingProgress;
using revenant::testing::VolumeRange;
using revenant::testing::wholeSourceScope;

constexpr std::size_t kBootSectorBytes = 512;

// The fixture's live JPEG: its bytes are a confidently recovered file's, so a
// hybrid run must not go looking for them again.
constexpr std::uint64_t kKeepJpegCluster = 16;

// The orphan is graded Uncertain, so accounting does not claim it — hybrid
// mode scans it anyway, which is the safety net the architecture asks for.
constexpr std::uint64_t kOrphanJpegCluster = 40;

// Small enough that the fixture volume takes several chunks, so a bounded
// checkpoint interval is observable on a 4 MiB image.
constexpr std::uint64_t kSmallChunkBytes = std::uint64_t{64} * 1024;

[[nodiscard]] CarverRegistry builtinRegistry() {
	CarverRegistry registry;
	registerBuiltinCarvers(registry);
	return registry;
}

// One recovery run over the fixture volume, to whatever plan a test states.
class RecoveryRun {
public:
	RecoveryRun(NtfsVolume& volume, RecoveryMode mode) : RecoveryRun(volume, freshRun(mode), 0) {}

	RecoveryRun(NtfsVolume& volume, const RecoveryPlan& plan, std::size_t stopAfter)
		: registry_(builtinRegistry()), scanner_(registry_, ScanConfig{}), progress_(stopAfter),
		  scope_(wholeSourceScope(volume.mount())),
		  stats_(HybridRecovery{scanner_, plan}.run(scope_, entries_, candidates_, progress_)) {}

	[[nodiscard]] const RecordingProgress& progress() const noexcept {
		return progress_;
	}

	[[nodiscard]] const revenant::Result<RecoveryStats>& stats() const noexcept {
		return stats_;
	}

	[[nodiscard]] const CollectingEntryVisitor& entries() const noexcept {
		return entries_;
	}

	[[nodiscard]] bool carvedAtCluster(std::uint64_t cluster) const {
		const auto offset = makeLayout().clusterOffsetBytes(cluster);
		return std::ranges::any_of(candidates_.candidates(), [offset](const ScanCandidate& found) {
			return found.offset == offset;
		});
	}

	[[nodiscard]] std::size_t candidateCount() const noexcept {
		return candidates_.candidates().size();
	}

private:
	CarverRegistry registry_;
	SignatureScanner scanner_;
	CollectingEntryVisitor entries_;
	CollectingVisitor candidates_;
	RecordingProgress progress_;
	// Declared before `stats_`: the run is sequenced by member initialization,
	// and it reads through this.
	RunScope scope_;
	revenant::Result<RecoveryStats> stats_;
};

TEST(HybridRecovery, FilesystemOnlyRecoversNamesAndCarvesNothing) {
	NtfsVolume volume;
	const RecoveryRun run{volume, RecoveryMode::kFilesystemOnly};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_EQ(run.stats().value().entriesReported, 4U);
	EXPECT_EQ(run.stats().value().candidatesReported, 0U);
	EXPECT_EQ(run.stats().value().regionsScanned, 0U);
	EXPECT_TRUE(run.stats().value().filesystemMounted);
}

TEST(HybridRecovery, CarveOnlyNeverTouchesTheFilesystem) {
	NtfsVolume volume;
	const RecoveryRun run{volume, RecoveryMode::kCarveOnly};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_EQ(run.stats().value().entriesReported, 0U);
	EXPECT_FALSE(run.stats().value().filesystemMounted);
	EXPECT_EQ(run.stats().value().accountedBytes, 0U);
	EXPECT_EQ(run.stats().value().regionsScanned, 1U);
}

// Carving alone sees every JPEG on the volume, named or not — it just cannot
// say which is which.
TEST(HybridRecovery, CarveOnlyFindsTheNamedFilesDataToo) {
	NtfsVolume volume;
	const RecoveryRun run{volume, RecoveryMode::kCarveOnly};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_TRUE(run.carvedAtCluster(kKeepJpegCluster));
	EXPECT_TRUE(run.carvedAtCluster(kUnallocatedJpegCluster));
}

TEST(HybridRecovery, HybridReportsBothSourcesInOneRun) {
	NtfsVolume volume;
	const RecoveryRun run{volume, RecoveryMode::kHybrid};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_EQ(run.stats().value().entriesReported, 4U);
	EXPECT_GT(run.stats().value().candidatesReported, 0U);
	EXPECT_TRUE(run.stats().value().filesystemMounted);
	EXPECT_GT(run.stats().value().accountedBytes, 0U);
}

// The one file neither source finds alone: no record points at it, so the
// filesystem cannot name it, and carving only reaches it because the pass ran.
TEST(HybridRecovery, HybridCarvesTheJpegNoRecordPointsAt) {
	NtfsVolume volume;
	const RecoveryRun run{volume, RecoveryMode::kHybrid};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_TRUE(run.carvedAtCluster(kUnallocatedJpegCluster));
}

TEST(HybridRecovery, HybridDoesNotLookWhereAConfidentEntryAlreadyAccounts) {
	NtfsVolume volume;
	const RecoveryRun run{volume, RecoveryMode::kHybrid};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_FALSE(run.carvedAtCluster(kKeepJpegCluster));
}

// An uncertain entry does not suppress carving: the region is a safety net.
TEST(HybridRecovery, HybridStillCarvesWhatOnlyAnUncertainEntryClaims) {
	NtfsVolume volume;
	const RecoveryRun run{volume, RecoveryMode::kHybrid};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_TRUE(run.carvedAtCluster(kOrphanJpegCluster));
}

// A formatted or RAW volume is exactly what carving is for, so hybrid mode
// downgrades rather than failing — and says so.
TEST(HybridRecovery, AVolumeThatWillNotMountStillCarvesInHybridMode) {
	NtfsVolume volume;
	volume.clear(VolumeRange{.offset = 0, .length = kBootSectorBytes});
	const RecoveryRun run{volume, RecoveryMode::kHybrid};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_FALSE(run.stats().value().filesystemMounted);
	EXPECT_EQ(run.stats().value().entriesReported, 0U);
	EXPECT_GT(run.candidateCount(), 0U);
}

// A zeroed boot sector is not a broken NTFS volume — no filesystem this build
// can read recognized it at all, which is what kNotFound says (story-0301).
TEST(HybridRecovery, AVolumeThatWillNotMountFailsAFilesystemOnlyRun) {
	NtfsVolume volume;
	volume.clear(VolumeRange{.offset = 0, .length = kBootSectorBytes});
	const RecoveryRun run{volume, RecoveryMode::kFilesystemOnly};
	ASSERT_FALSE(run.stats().hasValue());
	EXPECT_EQ(run.stats().error().code, ErrorCode::kNotFound);
}

TEST(HybridRecovery, HybridScansEveryGapTheNamesLeftBehind) {
	NtfsVolume volume;
	const RecoveryRun run{volume, RecoveryMode::kHybrid};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_GT(run.stats().value().regionsScanned, 1U);
	EXPECT_EQ(run.stats().value().regionsDropped, 0U);
}

TEST(HybridRecovery, ReportsTheSameEntriesTheFilesystemPassFound) {
	NtfsVolume volume;
	const RecoveryRun run{volume, RecoveryMode::kHybrid};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_EQ(run.entries().entries().size(), run.stats().value().entriesReported);
}

// A plan with a bounded checkpoint size is what makes an interrupted run cost a
// bounded amount of work: progress is reported once per chunk, not once per gap.
TEST(HybridRecovery, ReportsProgressOncePerBoundedChunk) {
	NtfsVolume volume;
	const RecoveryPlan plan{
		.mode = RecoveryMode::kCarveOnly,
		.resumeFrom = std::nullopt,
		.checkpointBytes = kSmallChunkBytes};
	const RecoveryRun run{volume, plan, 0};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_GT(run.progress().cursors().size(), 1U);
	EXPECT_EQ(run.progress().cursors().size(), run.stats().value().regionsScanned);
}

// The cursor is what a checkpoint stores, so it has to mean "everything before
// this has been searched".
TEST(HybridRecovery, ResumingSkipsWhatIsBehindTheCursor) {
	NtfsVolume volume;
	const RecoveryRun whole{volume, RecoveryMode::kCarveOnly};
	NtfsVolume second;
	const RecoveryPlan plan{
		.mode = RecoveryMode::kCarveOnly,
		.resumeFrom = makeLayout().clusterOffsetBytes(kUnallocatedJpegCluster),
		.checkpointBytes = kDefaultCheckpointBytes};
	const RecoveryRun resumed{second, plan, 0};
	ASSERT_TRUE(resumed.stats().hasValue());
	EXPECT_LT(resumed.candidateCount(), whole.candidateCount());
	EXPECT_TRUE(resumed.carvedAtCluster(kUnallocatedJpegCluster));
}

// A resumed run re-walks the volume for its byte accounting alone: those
// entries are already in the index, and appending them again would make every
// count after it wrong.
TEST(HybridRecovery, ResumingDoesNotReportTheFilesystemEntriesAgain) {
	NtfsVolume volume;
	const RecoveryPlan plan{
		.mode = RecoveryMode::kHybrid,
		.resumeFrom = 0,
		.checkpointBytes = kDefaultCheckpointBytes};
	const RecoveryRun run{volume, plan, 0};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_TRUE(run.entries().entries().empty());
	EXPECT_GT(run.stats().value().accountedBytes, 0U);
}

// What it found is real; what it has not read yet is why arbitration must wait.
TEST(HybridRecovery, AProgressReporterThatSaysStopEndsTheScanIncomplete) {
	NtfsVolume volume;
	const RecoveryPlan plan{
		.mode = RecoveryMode::kCarveOnly,
		.resumeFrom = std::nullopt,
		.checkpointBytes = kSmallChunkBytes};
	const RecoveryRun run{volume, plan, 1};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_FALSE(run.stats().value().scanComplete);
	EXPECT_EQ(run.progress().cursors().size(), 1U);
}

TEST(HybridRecovery, AScanThatRanToTheEndIsComplete) {
	NtfsVolume volume;
	const RecoveryRun run{volume, RecoveryMode::kHybrid};
	ASSERT_TRUE(run.stats().hasValue());
	EXPECT_TRUE(run.stats().value().scanComplete);
}

} // namespace
