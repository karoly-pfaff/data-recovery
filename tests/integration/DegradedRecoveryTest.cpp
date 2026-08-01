// SPDX-License-Identifier: GPL-3.0-or-later
// story-0604: a run over a device that will not give up one of its sectors. The
// file comes back — recovery must proceed past damage — and it comes back with
// zeros where the device refused. What is under test is that the run says so:
// the artifact names the invented bytes, the manifest carries the range, and the
// summary cannot be mistaken for an undamaged run's.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cli/RecoveryRun.hpp"
#include "cli/RunDelivery.hpp"
#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/SourceStack.hpp"
#include "revenant/recovery/CandidateIndex.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/IndexingVisitors.hpp"
#include "revenant/recovery/Manifest.hpp"
#include "revenant/recovery/RecoverySink.hpp"
#include "revenant/recovery/RunScope.hpp"
#include "support/FaultyDevice.hpp"
#include "support/FixtureContent.hpp"
#include "support/RecordingProgress.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::SourceStack;
using revenant::carve::CarverRegistry;
using revenant::carve::registerBuiltinCarvers;
using revenant::carve::ScanConfig;
using revenant::carve::SignatureScanner;
using revenant::cli::decideAndDeliver;
using revenant::cli::DeliverySource;
using revenant::cli::RunRequest;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::imagegen::ntfs::fixtureFiles;
using revenant::imagegen::ntfs::kDeletedJpegRecord;
using revenant::imagegen::ntfs::makeLayout;
using revenant::recovery::CandidateIndex;
using revenant::recovery::kWholeSource;
using revenant::testing::Fault;
using revenant::testing::FaultyDevice;
using revenant::testing::fixtureContentNamed;
using revenant::testing::readFileBytes;
using revenant::testing::readFileText;
using revenant::testing::RecordingProgress;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

constexpr std::uint32_t kSector = 512;

// One sector into the deleted JPEG's data rather than at its front: the file
// still parses as itself, so what the run reports is degradation and not a
// failure to recognize anything.
constexpr std::uint64_t kFaultWithinFile = kSector;

// Where `photos/deleted.jpg` keeps its first bytes — read from the fixture table
// rather than restated, so the fixture and the test cannot drift apart.
[[nodiscard]] std::uint64_t deletedJpegOffset() {
	const auto layout = makeLayout();
	for (const auto& file : fixtureFiles(layout)) {
		if (file.recordNumber == kDeletedJpegRecord) {
			return layout.clusterOffsetBytes(file.runs.front().startCluster);
		}
	}
	ADD_FAILURE() << "the fixture no longer holds a deleted JPEG";
	return 0;
}

// The whole tool over a device with one refused sector: discover, arbitrate,
// extract, record. Assembled here because a fault has to be injected at the
// device, which is below every path a command line can reach.
class DamagedRun {
public:
	DamagedRun()
		: faultOffset_(deletedJpegOffset() + kFaultWithinFile), image_(buildNtfsImage()),
		  stack_(
			  SourceStack::over(
				  std::make_unique<FaultyDevice>(
					  buildNtfsImage(),
					  kSector,
					  std::vector<Fault>{
						  Fault{.offsetBytes = faultOffset_, .lengthBytes = kSector}}))) {}

	[[nodiscard]] revenant::Result<revenant::cli::RunReport> run() {
		auto scope = revenant::recovery::RunScope::resolve(stack_.top(), kWholeSource);
		EXPECT_TRUE(scope.hasValue());
		const auto scanned = discover(scope.value());
		return deliver(scope.value(), scanned);
	}

	[[nodiscard]] std::uint64_t faultOffset() const noexcept {
		return faultOffset_;
	}

	[[nodiscard]] std::vector<std::byte> recovered(const std::string& relative) const {
		return readFileBytes(output_.path() / relative);
	}

	[[nodiscard]] std::string manifest() const {
		return readFileText(session_.path() / revenant::recovery::kManifestFileName);
	}

private:
	[[nodiscard]] RunRequest request() const {
		return RunRequest{
			.source = image_.path(),
			.destination = output_.path(),
			.session = session_.path(),
			.mode = revenant::recovery::RecoveryMode::kHybrid,
			.delivery = revenant::cli::Delivery::kExtract,
			.formats = {}};
	}

	[[nodiscard]] revenant::Result<revenant::cli::RunReport>
	deliver(revenant::recovery::RunScope& scope, const revenant::recovery::RecoveryStats& scanned) {
		auto sink = revenant::recovery::RecoverySink::open(output_.path(), image_.path());
		EXPECT_TRUE(sink.hasValue());
		const DeliverySource source{
			.device = &scope.device(),
			.stack = &stack_,
			.startBytes = scope.startBytes()};
		return decideAndDeliver(source, sink.value(), request(), scanned);
	}

	[[nodiscard]] revenant::recovery::RecoveryStats discover(revenant::recovery::RunScope& scope) {
		auto index = CandidateIndex::create(session_.path());
		EXPECT_TRUE(index.hasValue());
		return scanInto(scope, index.value());
	}

	[[nodiscard]] revenant::recovery::RecoveryStats
	scanInto(revenant::recovery::RunScope& scope, CandidateIndex& index) {
		revenant::recovery::IndexingEntryVisitor entries{index};
		revenant::recovery::IndexingCandidateVisitor candidates{index};
		RecordingProgress progress;
		const revenant::recovery::HybridRecovery hybrid{
			scanner_,
			revenant::recovery::freshRun(revenant::recovery::RecoveryMode::kHybrid)};
		const auto stats = hybrid.run(scope, entries, candidates, progress);
		EXPECT_TRUE(stats.hasValue());
		return stats.value();
	}

	[[nodiscard]] static CarverRegistry builtinRegistry() {
		CarverRegistry registry;
		registerBuiltinCarvers(registry);
		return registry;
	}

	std::uint64_t faultOffset_;
	TempFile image_;
	SourceStack stack_;
	CarverRegistry registry_{builtinRegistry()};
	SignatureScanner scanner_{registry_, ScanConfig{}};
	TempDir session_;
	TempDir output_;
};

[[nodiscard]] bool holds(const std::string& text, const std::string& fragment) {
	return text.find(fragment) != std::string::npos;
}

// The file is written whole and the run carries on: zero-filling is the
// behaviour, and this story removes the silence about it, not the behaviour.
TEST(DegradedRecovery, TheFileComesBackWholeWithZerosWhereTheDeviceRefused) {
	DamagedRun damaged;
	const auto report = damaged.run();
	ASSERT_TRUE(report.hasValue());

	auto expected = fixtureContentNamed("deleted.jpg");
	ASSERT_GT(expected.size(), kFaultWithinFile + kSector);
	std::fill_n(
		expected.begin() + static_cast<std::ptrdiff_t>(kFaultWithinFile),
		kSector,
		std::byte{0});
	EXPECT_EQ(damaged.recovered("photos/deleted.jpg"), expected);
}

// The point of the story: a file that validated on bytes this tool invented is
// recorded as such, per artifact and to the byte.
TEST(DegradedRecovery, TheArtifactNamesTheBytesItInvented) {
	DamagedRun damaged;
	const auto report = damaged.run();
	ASSERT_TRUE(report.hasValue());
	EXPECT_EQ(report.value().extraction.degraded, 1U);
	EXPECT_TRUE(holds(
		damaged.manifest(),
		R"("invented":[{"offset":)" + std::to_string(damaged.faultOffset()) +
			R"(,"length":512}])"));
}

TEST(DegradedRecovery, TheManifestCarriesTheRunsBadSectorMap) {
	DamagedRun damaged;
	const auto report = damaged.run();
	ASSERT_TRUE(report.hasValue());
	EXPECT_EQ(report.value().unreadableBytes, kSector);
	EXPECT_TRUE(holds(
		damaged.manifest(),
		R"("unreadable":[{"offset":)" + std::to_string(damaged.faultOffset()) +
			R"(,"length":512}])"));
}

// Everything else the run recovered is untouched: marking is an intersection,
// not a flag on the run.
TEST(DegradedRecovery, ArtifactsThatMissTheFaultAreNotMarked) {
	DamagedRun damaged;
	const auto report = damaged.run();
	ASSERT_TRUE(report.hasValue());
	// Exactly one, so the marking is an intersection and not a flag on the run:
	// several artifacts were written and only the one over the fault is degraded.
	EXPECT_EQ(report.value().extraction.degraded, 1U);
	EXPECT_GT(report.value().extraction.filesWritten, 1U);
	EXPECT_EQ(damaged.recovered("photos/keep.jpg"), fixtureContentNamed("keep.jpg"));
	EXPECT_TRUE(holds(damaged.manifest(), R"("writtenName":"photos/keep.jpg")"));
	EXPECT_TRUE(holds(damaged.manifest(), R"("invented":[])"));
}

} // namespace
