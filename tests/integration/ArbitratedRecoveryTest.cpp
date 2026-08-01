// SPDX-License-Identifier: GPL-3.0-or-later
// The story-0112 proof: a real hybrid run over the fixture image, everything
// it finds written to a durable index, and the index arbitrated. What comes
// out is the ADR-0006 behaviour — one explanation per region, names beating
// anonymous carves of the same bytes.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/recovery/Arbitration.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/CandidateIndex.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/IndexingVisitors.hpp"
#include "support/RecordingProgress.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"
#include "support/WholeSourceScope.hpp"

namespace {

using revenant::Confidence;
using revenant::ImageFileDevice;
using revenant::carve::CarverRegistry;
using revenant::carve::registerBuiltinCarvers;
using revenant::carve::ScanConfig;
using revenant::carve::SignatureScanner;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::imagegen::ntfs::kUnallocatedJpegCluster;
using revenant::imagegen::ntfs::makeLayout;
using revenant::recovery::arbitrateIndex;
using revenant::recovery::Candidate;
using revenant::recovery::CandidateIndex;
using revenant::recovery::CandidateSource;
using revenant::recovery::freshRun;
using revenant::recovery::HybridRecovery;
using revenant::recovery::IndexingCandidateVisitor;
using revenant::recovery::IndexingEntryVisitor;
using revenant::recovery::RecoveryMode;
using revenant::testing::RecordingProgress;
using revenant::testing::TempDir;
using revenant::testing::TempFile;
using revenant::testing::wholeSourceScope;

[[nodiscard]] std::unique_ptr<ImageFileDevice> openDevice(const TempFile& file) {
	return std::move(ImageFileDevice::open(file.path()).value());
}

[[nodiscard]] CarverRegistry builtinRegistry() {
	CarverRegistry registry;
	registerBuiltinCarvers(registry);
	return registry;
}

// A whole run: hybrid recovery over the fixture image, indexed into a session
// directory, then arbitrated — the path `revenant-undelete` will take.
class ArbitratedRecovery : public ::testing::Test {
protected:
	ArbitratedRecovery()
		: file_(buildNtfsImage()), device_(openDevice(file_)), registry_(builtinRegistry()),
		  scanner_(registry_, ScanConfig{}) {
		indexRun();
	}

	[[nodiscard]] const std::vector<Candidate>& winners() const noexcept {
		return winners_;
	}

	[[nodiscard]] std::uint64_t suppressed() const noexcept {
		return suppressed_;
	}

	[[nodiscard]] const Candidate* winnerNamed(std::string_view name) const {
		const auto found = std::ranges::find(winners_, name, &Candidate::name);
		return found != winners_.end() ? &*found : nullptr;
	}

	[[nodiscard]] bool anyWinnerCovers(std::uint64_t cluster) const {
		const auto offset = makeLayout().clusterOffsetBytes(cluster);
		return std::ranges::any_of(winners_, [offset](const Candidate& winner) {
			return std::ranges::any_of(winner.extents, [offset](const auto& extent) {
				return extent.deviceOffset <= offset &&
					   offset < extent.deviceOffset + extent.lengthBytes;
			});
		});
	}

private:
	void indexRun() {
		auto index = CandidateIndex::create(session_.path());
		ASSERT_TRUE(index.hasValue());
		discoverInto(index.value());
		decide();
	}

	void discoverInto(CandidateIndex& index) {
		IndexingEntryVisitor entries{index};
		IndexingCandidateVisitor candidates{index};
		runInto(entries, candidates);
		EXPECT_EQ(entries.failedAppends() + candidates.failedAppends(), 0U);
	}

	void runInto(IndexingEntryVisitor& entries, IndexingCandidateVisitor& candidates) {
		const HybridRecovery recovery{scanner_, freshRun(RecoveryMode::kHybrid)};
		RecordingProgress progress;
		auto scope = wholeSourceScope(*device_);
		EXPECT_TRUE(recovery.run(scope, entries, candidates, progress).hasValue());
	}

	void decide() {
		auto decided = arbitrateIndex(session_.path());
		ASSERT_TRUE(decided.hasValue());
		winners_ = std::move(decided.value().winners);
		suppressed_ = decided.value().suppressed;
	}

	TempFile file_;
	std::unique_ptr<ImageFileDevice> device_;
	CarverRegistry registry_;
	SignatureScanner scanner_;
	TempDir session_;
	std::vector<Candidate> winners_;
	std::uint64_t suppressed_ = 0;
};

TEST_F(ArbitratedRecovery, TheNamedFilesAllSurviveArbitration) {
	EXPECT_NE(winnerNamed("photos/keep.jpg"), nullptr);
	EXPECT_NE(winnerNamed("photos/deleted.jpg"), nullptr);
	EXPECT_NE(winnerNamed("notes.txt"), nullptr);
	EXPECT_NE(winnerNamed("orphan.jpg"), nullptr);
}

// The whole point of the hybrid run, still standing after arbitration.
TEST_F(ArbitratedRecovery, TheUnreferencedJpegSurvivesAsACarvedWinner) {
	EXPECT_TRUE(anyWinnerCovers(kUnallocatedJpegCluster));
	const auto* carved = winnerNamed("jpg");
	ASSERT_NE(carved, nullptr);
	EXPECT_EQ(carved->source, CandidateSource::kCarve);
}

// The orphan is claimed by both sources: the filesystem knows its name, the
// carve pass found the same bytes. Exactly one of them may win the region.
TEST_F(ArbitratedRecovery, OnlyOneExplanationSurvivesPerRegion) {
	EXPECT_GT(suppressed(), 0U);
	EXPECT_TRUE(std::ranges::none_of(winners(), [](const Candidate& winner) {
		return winner.confidence == Confidence::kRejected;
	}));
}

// A named entry beats the anonymous carve of its own bytes: the orphan comes
// back as `orphan.jpg`, not as `jpg`.
TEST_F(ArbitratedRecovery, TheOrphanKeepsItsNameRatherThanBecomingACarve) {
	const auto* orphan = winnerNamed("orphan.jpg");
	ASSERT_NE(orphan, nullptr);
	EXPECT_EQ(orphan->source, CandidateSource::kFilesystem);
}

TEST_F(ArbitratedRecovery, NoTwoWinnersClaimTheSameByte) {
	std::vector<std::pair<std::uint64_t, std::uint64_t>> spans;
	for (const auto& winner : winners()) {
		for (const auto& extent : winner.extents) {
			spans.emplace_back(extent.deviceOffset, extent.deviceOffset + extent.lengthBytes);
		}
	}
	std::ranges::sort(spans);
	for (std::size_t i = 1; i < spans.size(); ++i) {
		EXPECT_LE(spans.at(i - 1).second, spans.at(i).first);
	}
}

TEST_F(ArbitratedRecovery, TheResidentFileKeepsItsBytesThroughTheIndex) {
	const auto* notes = winnerNamed("notes.txt");
	ASSERT_NE(notes, nullptr);
	EXPECT_TRUE(notes->extents.empty());
	EXPECT_FALSE(notes->residentContent.empty());
}

} // namespace
