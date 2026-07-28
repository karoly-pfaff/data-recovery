// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/HybridRecovery.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/ScanCandidate.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "support/CollectingEntryVisitor.hpp"
#include "support/CollectingVisitor.hpp"
#include "support/NtfsVolume.hpp"

namespace {

using revenant::ErrorCode;
using revenant::carve::CarverRegistry;
using revenant::carve::registerBuiltinCarvers;
using revenant::carve::ScanCandidate;
using revenant::carve::ScanConfig;
using revenant::carve::SignatureScanner;
using revenant::imagegen::ntfs::kUnallocatedJpegCluster;
using revenant::imagegen::ntfs::makeLayout;
using revenant::recovery::HybridRecovery;
using revenant::recovery::RecoveryMode;
using revenant::recovery::RecoveryStats;
using revenant::testing::CollectingEntryVisitor;
using revenant::testing::CollectingVisitor;
using revenant::testing::NtfsVolume;
using revenant::testing::VolumeRange;

constexpr std::size_t kBootSectorBytes = 512;

// The fixture's live JPEG: its bytes are a confidently recovered file's, so a
// hybrid run must not go looking for them again.
constexpr std::uint64_t kKeepJpegCluster = 16;

// The orphan is graded Uncertain, so accounting does not claim it — hybrid
// mode scans it anyway, which is the safety net the architecture asks for.
constexpr std::uint64_t kOrphanJpegCluster = 40;

[[nodiscard]] CarverRegistry builtinRegistry() {
	CarverRegistry registry;
	registerBuiltinCarvers(registry);
	return registry;
}

// One recovery run over the fixture volume, in one mode.
class RecoveryRun {
public:
	RecoveryRun(NtfsVolume& volume, RecoveryMode mode)
		: registry_(builtinRegistry()), scanner_(registry_, ScanConfig{}),
		  stats_(HybridRecovery{scanner_, mode}.run(volume.mount(), entries_, candidates_)) {}

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

TEST(HybridRecovery, AVolumeThatWillNotMountFailsAFilesystemOnlyRun) {
	NtfsVolume volume;
	volume.clear(VolumeRange{.offset = 0, .length = kBootSectorBytes});
	const RecoveryRun run{volume, RecoveryMode::kFilesystemOnly};
	ASSERT_FALSE(run.stats().hasValue());
	EXPECT_EQ(run.stats().error().code, ErrorCode::kInvalidArgument);
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

} // namespace
