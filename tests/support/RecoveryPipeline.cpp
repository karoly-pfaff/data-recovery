// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/RecoveryPipeline.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

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
#include "support/RecordingProgress.hpp"
#include "support/TempFile.hpp"
#include "support/WholeSourceScope.hpp"

namespace revenant::testing {

namespace {

using revenant::carve::CarverRegistry;
using revenant::carve::registerBuiltinCarvers;

[[nodiscard]] std::unique_ptr<revenant::ImageFileDevice> openDevice(const TempFile& file) {
	return std::move(revenant::ImageFileDevice::open(file.path()).value());
}

[[nodiscard]] CarverRegistry builtinRegistry() {
	CarverRegistry registry;
	registerBuiltinCarvers(registry);
	return registry;
}

} // namespace

RecoveryPipeline::RecoveryPipeline()
	: image_(revenant::imagegen::ntfs::buildNtfsImage()), device_(openDevice(image_)),
	  registry_(builtinRegistry()), scanner_(registry_, revenant::carve::ScanConfig{}) {}

void RecoveryPipeline::runFullRecovery() {
	discover();
	extractWinners();
}

std::filesystem::path RecoveryPipeline::recovered(std::string_view relative) const {
	return output_.path() / std::filesystem::path{relative};
}

void RecoveryPipeline::discover() {
	auto index = revenant::recovery::CandidateIndex::create(session_.path());
	revenant::recovery::IndexingEntryVisitor entries{index.value()};
	revenant::recovery::IndexingCandidateVisitor candidates{index.value()};
	runHybrid(entries, candidates);
}

void RecoveryPipeline::runHybrid(
	revenant::recovery::IndexingEntryVisitor& entries,
	revenant::recovery::IndexingCandidateVisitor& candidates) {
	const revenant::recovery::HybridRecovery recovery{
		scanner_,
		revenant::recovery::freshRun(revenant::recovery::RecoveryMode::kHybrid)};
	RecordingProgress progress;
	auto scope = wholeSourceScope(*device_);
	EXPECT_TRUE(recovery.run(scope, entries, candidates, progress).hasValue());
}

void RecoveryPipeline::extractWinners() {
	auto decided = revenant::recovery::arbitrateIndex(session_.path());
	ASSERT_TRUE(decided.hasValue());
	writeWinners(decided.value().winners);
}

void RecoveryPipeline::writeWinners(const std::vector<revenant::recovery::Candidate>& winners) {
	auto sink = revenant::recovery::RecoverySink::open(output_.path(), image_.path());
	stats_ = sink.value().extract(winners, *device_).stats;
}

} // namespace revenant::testing
