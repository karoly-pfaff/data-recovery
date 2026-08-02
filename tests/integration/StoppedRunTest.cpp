// SPDX-License-Identifier: GPL-3.0-or-later
// story-0605: a run that loses its device still ends with a usable result. What
// is under test is the ending: the run stops promptly instead of transcribing a
// corpse as zeros, everything already recovered stays, the manifest says where
// it stopped, and the exit status says what to do next.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cli/RecoveryRun.hpp"
#include "cli/RunDelivery.hpp"
#include "cli/RunOutcome.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/RetryingDevice.hpp"
#include "revenant/core/io/SourceStack.hpp"
#include "revenant/recovery/CandidateIndex.hpp"
#include "revenant/recovery/Checkpoint.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/IndexingVisitors.hpp"
#include "revenant/recovery/Manifest.hpp"
#include "revenant/recovery/RecoverySink.hpp"
#include "revenant/recovery/RunScope.hpp"
#include "support/BuiltinRegistry.hpp"
#include "support/FaultyDevice.hpp"
#include "support/FixtureContent.hpp"
#include "support/RecordingProgress.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::SourceStack;
using revenant::carve::ScanConfig;
using revenant::carve::SignatureScanner;
using revenant::cli::decideAndDeliver;
using revenant::cli::DeliverySource;
using revenant::cli::outcomeOf;
using revenant::cli::RunOutcome;
using revenant::cli::RunRequest;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::recovery::CandidateIndex;
using revenant::recovery::kWholeSource;
using revenant::testing::builtinRegistry;
using revenant::testing::Fault;
using revenant::testing::FaultyDevice;
using revenant::testing::readFileText;
using revenant::testing::RecordingProgress;
using revenant::testing::TempDir;
using revenant::testing::TempFile;
using revenant::testing::WhenLost;

constexpr std::uint32_t kSector = 512;

// The pause is for a real drive's own error recovery; a test that waited for it
// would spend four hundred seconds measuring the clock.
constexpr revenant::RetryPolicy kNoWaiting{.attempts = 3, .pause = std::chrono::milliseconds{0}};

// Where the device is told to stop answering. Past the NTFS boot sector and the
// `$MFT`, so the filesystem pass gets through and the run has something to have
// recovered before it loses the disk — which is the case worth testing.
constexpr std::uint64_t kDiesAt = std::uint64_t{2} << 20U;

// A whole run over a device that goes away in the middle of it.
class LostDeviceRun {
public:
	LostDeviceRun()
		: image_(buildNtfsImage()),
		  stack_(
			  SourceStack::over(
				  std::make_unique<FaultyDevice>(
					  buildNtfsImage(),
					  kSector,
					  std::vector<Fault>{Fault{.offsetBytes = kDiesAt, .lengthBytes = kSector}},
					  WhenLost::kAfterTheFirstFault),
				  kNoWaiting)) {}

	[[nodiscard]] revenant::Result<revenant::cli::RunReport> run() {
		auto scope = revenant::recovery::RunScope::resolve(stack_.top(), kWholeSource);
		EXPECT_TRUE(scope.hasValue());
		return deliver(scope.value(), discover(scope.value()));
	}

	[[nodiscard]] std::string manifest() const {
		return readFileText(session_.path() / revenant::recovery::kManifestFileName);
	}

	[[nodiscard]] bool hasCheckpoint() const {
		return revenant::recovery::readCheckpoint(session_.path()).hasValue();
	}

private:
	[[nodiscard]] RunRequest request() const {
		return RunRequest{
			.source = image_.path(),
			.destination = output_.path(),
			.session = session_.path(),
			.mode = revenant::recovery::RecoveryMode::kCarveOnly,
			.delivery = revenant::cli::Delivery::kExtract,
			.formats = {}};
	}

	[[nodiscard]] revenant::Result<revenant::cli::RunReport> deliver(
		revenant::recovery::RunScope& scope,
		const revenant::Result<revenant::recovery::RecoveryStats>& scanned) {
		auto sink = revenant::recovery::RecoverySink::open(output_.path(), image_.path());
		EXPECT_TRUE(sink.hasValue());
		return decideAndDeliver(
			DeliverySource::of(stack_, scope),
			sink.value(),
			request(),
			scanned);
	}

	[[nodiscard]] revenant::Result<revenant::recovery::RecoveryStats>
	discover(revenant::recovery::RunScope& scope) {
		auto index = CandidateIndex::create(session_.path());
		EXPECT_TRUE(index.hasValue());
		return scanInto(scope, index.value());
	}

	[[nodiscard]] revenant::Result<revenant::recovery::RecoveryStats>
	scanInto(revenant::recovery::RunScope& scope, CandidateIndex& index) {
		revenant::recovery::IndexingEntryVisitor entries{index};
		revenant::recovery::IndexingCandidateVisitor candidates{index};
		RecordingProgress progress;
		return hybrid_.run(scope, entries, candidates, progress);
	}

	TempFile image_;
	SourceStack stack_;
	revenant::carve::CarverRegistry registry_{builtinRegistry()};
	SignatureScanner scanner_{registry_, ScanConfig{}};
	revenant::recovery::HybridRecovery hybrid_{
		scanner_,
		revenant::recovery::freshRun(revenant::recovery::RecoveryMode::kCarveOnly)};
	TempDir session_;
	TempDir output_;
};

[[nodiscard]] bool holds(const std::string& text, const std::string& fragment) {
	return text.find(fragment) != std::string::npos;
}

// The run ends, and it ends as a lost source rather than as a device full of
// zeros. Without the give-up bound the retry layer would have zero-filled every
// sector from `kDiesAt` to the end of the image and reported a clean run.
TEST(StoppedRun, ALostDeviceEndsTheRunAsALostSource) {
	LostDeviceRun lost;
	const auto report = lost.run();
	ASSERT_FALSE(report.hasValue());
	EXPECT_EQ(report.error().code, revenant::ErrorCode::kSourceLost);
}

// Exit 3: re-running the same command carries on. Nothing needs fixing first —
// which is the whole difference between this stop and a full destination.
TEST(StoppedRun, ALostDeviceExitsResumable) {
	LostDeviceRun lost;
	const auto report = lost.run();
	ASSERT_FALSE(report.hasValue());
	EXPECT_EQ(outcomeOf(report.error().code), RunOutcome::kStoppedResumable);
}

// And it leaves the record. A stop that says nothing is what this story was
// written against: the manifest names the ending and where the device stopped
// answering, so a second run and a later reader both know.
TEST(StoppedRun, ALostDeviceLeavesAManifestSayingSo) {
	LostDeviceRun lost;
	ASSERT_FALSE(lost.run().hasValue());
	const auto text = lost.manifest();
	EXPECT_TRUE(holds(text, R"("outcome":"stopped-resumable")"));
	std::cerr << "MANIFEST: " << text << '\n';
}

} // namespace
