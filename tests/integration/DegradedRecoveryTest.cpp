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
#include <utility>
#include <vector>

#include "cli/RecoveryRun.hpp"
#include "cli/RunDelivery.hpp"
#include "imagegen/disk/DiskImageBuilder.hpp"
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
using revenant::imagegen::disk::buildMbrDiskImage;
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

// What source a run works over, and which of its sectors the device refuses.
// The offsets are device-absolute in both cases, which is the coordinate system
// the manifest states and the only one a fault can be injected in.
struct Damaged {
	std::vector<std::byte> image;
	std::uint64_t faultOffset;
	std::uint32_t partition;
	// How many carve regions the scan gets through before its progress reporter
	// says to stop. Zero means "all of them", which is every case but the
	// interrupted one.
	std::size_t stopAfter = 0;
	revenant::cli::Delivery delivery = revenant::cli::Delivery::kExtract;
};

// An image of one volume, with a sector of `photos/deleted.jpg` refused.
[[nodiscard]] Damaged aDamagedVolume() {
	return Damaged{
		.image = buildNtfsImage(),
		.faultOffset = deletedJpegOffset() + kFaultWithinFile,
		.partition = kWholeSource};
}

// The same damage, previewed rather than extracted. Nothing is written, and the
// overlap is a fact about where the artifacts live rather than about writing,
// so the run still reports it — and must not claim a file was written.
[[nodiscard]] Damaged aPreviewOverDamage() {
	Damaged previewed = aDamagedVolume();
	previewed.delivery = revenant::cli::Delivery::kPreview;
	return previewed;
}

// The same damage, with the scan stopped after its first carve region. Nothing
// is decided and nothing is written, but the damage the scan already met is a
// fact about the disk that the next run inherits.
[[nodiscard]] Damaged anInterruptedRunOverDamage() {
	Damaged interrupted = aDamagedVolume();
	interrupted.stopAfter = 1;
	return interrupted;
}

// The same damage, on the same volume, as partition 1 of a whole disk. The run
// is scoped, so its extents are relative to the window while the map is not —
// which is the translation `RunScope::startBytes()` exists for.
[[nodiscard]] Damaged aDamagedPartition() {
	const auto disk = buildMbrDiskImage();
	const auto ntfsAt = disk.volumeOffsets.front();
	return Damaged{
		.image = disk.bytes,
		.faultOffset = ntfsAt + deletedJpegOffset() + kFaultWithinFile,
		.partition = 1};
}

// The whole tool over a device with one refused sector: discover, arbitrate,
// extract, record. Assembled here because a fault has to be injected at the
// device, which is below every path a command line can reach.
class DamagedRun {
public:
	explicit DamagedRun(Damaged damaged)
		: faultOffset_(damaged.faultOffset), partition_(damaged.partition),
		  stopAfter_(damaged.stopAfter), delivery_(damaged.delivery), image_(damaged.image),
		  stack_(
			  SourceStack::over(
				  std::make_unique<FaultyDevice>(
					  std::move(damaged.image),
					  kSector,
					  std::vector<Fault>{
						  Fault{.offsetBytes = faultOffset_, .lengthBytes = kSector}}))) {}

	[[nodiscard]] revenant::Result<revenant::cli::RunReport> run() {
		auto scope = revenant::recovery::RunScope::resolve(stack_.top(), partition_);
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
			.delivery = delivery_,
			.formats = {}};
	}

	[[nodiscard]] revenant::Result<revenant::cli::RunReport>
	deliver(revenant::recovery::RunScope& scope, const revenant::recovery::RecoveryStats& scanned) {
		auto sink = revenant::recovery::RecoverySink::open(output_.path(), image_.path());
		EXPECT_TRUE(sink.hasValue());
		return decideAndDeliver(
			DeliverySource::of(stack_, scope),
			sink.value(),
			request(),
			scanned);
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
		return hybridOver(scope, entries, candidates);
	}

	[[nodiscard]] revenant::recovery::RecoveryStats hybridOver(
		revenant::recovery::RunScope& scope,
		revenant::recovery::IndexingEntryVisitor& entries,
		revenant::recovery::IndexingCandidateVisitor& candidates) {
		RecordingProgress progress{stopAfter_};
		const auto stats = hybrid_.run(scope, entries, candidates, progress);
		EXPECT_TRUE(stats.hasValue());
		return stats.value();
	}

	[[nodiscard]] static CarverRegistry builtinRegistry() {
		CarverRegistry registry;
		registerBuiltinCarvers(registry);
		return registry;
	}

	std::uint64_t faultOffset_;
	std::uint32_t partition_;
	std::size_t stopAfter_;
	revenant::cli::Delivery delivery_;
	TempFile image_;
	SourceStack stack_;
	CarverRegistry registry_{builtinRegistry()};
	SignatureScanner scanner_{registry_, ScanConfig{}};
	revenant::recovery::HybridRecovery hybrid_{
		scanner_,
		revenant::recovery::freshRun(revenant::recovery::RecoveryMode::kHybrid)};
	TempDir session_;
	TempDir output_;
};

[[nodiscard]] bool holds(const std::string& text, const std::string& fragment) {
	return text.find(fragment) != std::string::npos;
}

// The file is written whole and the run carries on: zero-filling is the
// behaviour, and this story removes the silence about it, not the behaviour.
TEST(DegradedRecovery, TheFileComesBackWholeWithZerosWhereTheDeviceRefused) {
	DamagedRun damaged{aDamagedVolume()};
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
	DamagedRun damaged{aDamagedVolume()};
	const auto report = damaged.run();
	ASSERT_TRUE(report.hasValue());
	EXPECT_EQ(report.value().extraction.degraded, 1U);
	EXPECT_TRUE(holds(
		damaged.manifest(),
		R"("invented":[{"offset":)" + std::to_string(damaged.faultOffset()) +
			R"(,"length":512}])"));
}

TEST(DegradedRecovery, TheManifestCarriesTheRunsBadSectorMap) {
	DamagedRun damaged{aDamagedVolume()};
	const auto report = damaged.run();
	ASSERT_TRUE(report.hasValue());
	EXPECT_EQ(report.value().unreadableBytes, kSector);
	EXPECT_TRUE(holds(
		damaged.manifest(),
		R"("unreadable":[{"offset":)" + std::to_string(damaged.faultOffset()) +
			R"(,"length":512}])"));
}

// The same damage inside partition 1 of a whole disk. The run's extents are
// relative to the window and the map is device-absolute, so the offset the
// manifest states is only right if `RunScope::startBytes()` supplied the
// window's own offset — a `startBytes()` stuck at zero leaves the two
// coordinate systems a megabyte apart and marks nothing at all.
TEST(DegradedRecovery, AScopedRunMarksTheFaultAtItsDeviceAbsoluteOffset) {
	DamagedRun damaged{aDamagedPartition()};
	const auto report = damaged.run();
	ASSERT_TRUE(report.hasValue());
	EXPECT_EQ(report.value().extraction.degraded, 1U);
	EXPECT_TRUE(holds(
		damaged.manifest(),
		R"("invented":[{"offset":)" + std::to_string(damaged.faultOffset()) +
			R"(,"length":512}])"));
	// And the artifact's own extents count from the same place, so the two range
	// lists in one record can be compared with each other. Untranslated, the
	// file would appear to live a megabyte away from the damage inside it.
	EXPECT_TRUE(holds(
		damaged.manifest(),
		R"("extents":[{"offset":)" + std::to_string(damaged.faultOffset() - kFaultWithinFile) +
			","));
}

// A preview writes nothing, so nothing can have been written with invented
// bytes — but the artifacts it names still sit on damage, and the run says so.
TEST(DegradedRecovery, APreviewReportsTheDamageItWouldHaveWrittenThrough) {
	DamagedRun damaged{aPreviewOverDamage()};
	const auto report = damaged.run();
	ASSERT_TRUE(report.hasValue());
	EXPECT_EQ(report.value().extraction.filesWritten, 0U);
	EXPECT_EQ(report.value().unreadableBytes, kSector);
	EXPECT_EQ(report.value().extraction.degraded, 1U);
}

// An interrupted run decided nothing and wrote nothing, but it read — and what
// the device refused it is still the operator's to know.
TEST(DegradedRecovery, AnInterruptedRunStillReportsWhatTheDeviceRefused) {
	DamagedRun damaged{anInterruptedRunOverDamage()};
	const auto report = damaged.run();
	ASSERT_TRUE(report.hasValue());
	EXPECT_FALSE(report.value().discovery.scanComplete);
	EXPECT_EQ(report.value().unreadableBytes, kSector);
}

TEST(DegradedRecovery, ArtifactsThatMissTheFaultAreNotMarked) {
	DamagedRun damaged{aDamagedVolume()};
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
