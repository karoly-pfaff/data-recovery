// SPDX-License-Identifier: GPL-3.0-or-later
// The M1 vertical slice, whole: a real image file mounted read-only, recovered
// by both sources, indexed, arbitrated, and written out — then every file on
// disk compared byte-for-byte against the fixture that produced it.
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/recovery/Arbitration.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/CandidateIndex.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/IndexingVisitors.hpp"
#include "revenant/recovery/RecoverySink.hpp"
#include "support/FixtureContent.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::ImageFileDevice;
using revenant::carve::CarverRegistry;
using revenant::carve::registerBuiltinCarvers;
using revenant::carve::ScanConfig;
using revenant::carve::SignatureScanner;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::imagegen::ntfs::unallocatedJpeg;
using revenant::recovery::arbitrateIndex;
using revenant::recovery::CandidateIndex;
using revenant::recovery::ExtractionStats;
using revenant::recovery::HybridRecovery;
using revenant::recovery::IndexingCandidateVisitor;
using revenant::recovery::IndexingEntryVisitor;
using revenant::recovery::RecoveryMode;
using revenant::recovery::RecoverySink;
using revenant::testing::fixtureContentNamed;
using revenant::testing::readFileBytes;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

[[nodiscard]] std::unique_ptr<ImageFileDevice> openDevice(const TempFile& file) {
	return std::move(ImageFileDevice::open(file.path()).value());
}

[[nodiscard]] CarverRegistry builtinRegistry() {
	CarverRegistry registry;
	registerBuiltinCarvers(registry);
	return registry;
}

// The whole tool, once: mount, recover, index, arbitrate, extract.
class RecoveredFiles : public ::testing::Test {
protected:
	RecoveredFiles()
		: image_(buildNtfsImage()), device_(openDevice(image_)), registry_(builtinRegistry()),
		  scanner_(registry_, ScanConfig{}) {
		runEverything();
	}

	[[nodiscard]] std::filesystem::path recovered(std::string_view relative) const {
		return output_.path() / std::filesystem::path{relative};
	}

	[[nodiscard]] const ExtractionStats& stats() const noexcept {
		return stats_;
	}

private:
	void runEverything() {
		discover();
		extractWinners();
	}

	void discover() {
		auto index = CandidateIndex::create(session_.path());
		IndexingEntryVisitor entries{index.value()};
		IndexingCandidateVisitor candidates{index.value()};
		runHybrid(entries, candidates);
	}

	void runHybrid(IndexingEntryVisitor& entries, IndexingCandidateVisitor& candidates) {
		const HybridRecovery recovery{scanner_, RecoveryMode::kHybrid};
		EXPECT_TRUE(recovery.run(*device_, entries, candidates).hasValue());
	}

	void extractWinners() {
		auto decided = arbitrateIndex(session_.path());
		ASSERT_TRUE(decided.hasValue());
		writeWinners(decided.value().winners);
	}

	void writeWinners(const std::vector<revenant::recovery::Candidate>& winners) {
		auto sink = RecoverySink::open(output_.path(), image_.path());
		stats_ = sink.value().extract(winners, *device_);
	}

	TempFile image_;
	std::unique_ptr<ImageFileDevice> device_;
	CarverRegistry registry_;
	SignatureScanner scanner_;
	TempDir session_;
	TempDir output_;
	ExtractionStats stats_{.filesWritten = 0, .bytesWritten = 0, .failed = 0, .renamed = 0};
};

TEST_F(RecoveredFiles, EveryWinnerLandedAndNoneFailed) {
	EXPECT_GT(stats().filesWritten, 0U);
	EXPECT_EQ(stats().failed, 0U);
}

// The deleted, fragmented JPEG comes back at its own path, with its own bytes.
// This is the sentence the whole milestone exists to be able to say.
TEST_F(RecoveredFiles, TheDeletedFragmentedJpegIsBackAtItsPathAndByteIdentical) {
	const auto written = recovered("photos/deleted.jpg");
	ASSERT_TRUE(std::filesystem::exists(written));
	EXPECT_EQ(readFileBytes(written), fixtureContentNamed("deleted.jpg"));
}

TEST_F(RecoveredFiles, TheLiveAndResidentFilesComeBackToo) {
	EXPECT_EQ(readFileBytes(recovered("photos/keep.jpg")), fixtureContentNamed("keep.jpg"));
	EXPECT_EQ(readFileBytes(recovered("notes.txt")), fixtureContentNamed("notes.txt"));
}

TEST_F(RecoveredFiles, TheOrphanKeepsItsNameEvenWithoutItsParent) {
	EXPECT_EQ(readFileBytes(recovered("orphan.jpg")), fixtureContentNamed("orphan.jpg"));
}

// No record points at it, so it has no name to keep — it comes back carved,
// bucketed by format, and exact.
TEST_F(RecoveredFiles, TheUnreferencedJpegComesBackUnderTheCarvedBucket) {
	const auto written = recovered("carved/jpg/f00000004.jpg");
	ASSERT_TRUE(std::filesystem::exists(written));
	EXPECT_EQ(readFileBytes(written), unallocatedJpeg());
}

TEST_F(RecoveredFiles, NothingWasWrittenOutsideTheDestination) {
	EXPECT_EQ(stats().renamed, 0U);
	EXPECT_TRUE(std::filesystem::exists(recovered("photos")));
}

} // namespace
