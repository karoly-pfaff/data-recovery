// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/HybridRecovery.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "recovery/PartitionedWalk.hpp"
#include "recovery/ScanRegions.hpp"
#include "recovery/VolumeWalk.hpp"
#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/recovery/ByteAccounting.hpp"
#include "revenant/recovery/RunScope.hpp"

namespace revenant::recovery {

namespace {

// Everything the filesystem pass finds passes through here on its way to the
// caller. The run needs to know what was accounted for and how much was
// reported, and neither can be asked afterwards without holding every entry in
// memory — which is exactly what a terabyte device forbids.
class AccountingVisitor final : public fs::EntryVisitor {
public:
	AccountingVisitor(fs::EntryVisitor& downstream, ByteAccounting& accounting) noexcept
		: downstream_(&downstream), accounting_(&accounting) {}

	void onEntry(const fs::RecoveredEntry& entry) override {
		accounting_->account(entry);
		downstream_->onEntry(entry);
		++reported_;
	}

	[[nodiscard]] std::uint64_t reported() const noexcept {
		return reported_;
	}

private:
	fs::EntryVisitor* downstream_;
	ByteAccounting* accounting_;
	std::uint64_t reported_ = 0;
};

// Where a resumed run's entries go. The volume is re-walked for the byte
// accounting the carve gaps come from, and for nothing else: these entries are
// already in the index, and appending them a second time would make every count
// after it wrong.
class DroppingVisitor final : public fs::EntryVisitor {
public:
	void onEntry(const fs::RecoveredEntry& /*entry*/) override {}
};

// The filesystem pass over whatever the scope resolved to: every partition of a
// disk, or the one volume a scoped run and an unpartitioned image both are. The
// choice was made when the table was read; neither of these two reads it again.
[[nodiscard]] Result<fs::EnumerationStats> walkLayout(RunScope& scope, fs::EntryVisitor& visitor) {
	if (scope.layout().empty()) {
		return enumerateVolume(scope.device(), visitor);
	}
	return enumerateDisk(scope.device(), scope.layout(), visitor);
}

} // namespace

RecoveryPlan freshRun(RecoveryMode mode) noexcept {
	return RecoveryPlan{
		.mode = mode,
		.resumeFrom = std::nullopt,
		.checkpointBytes = kDefaultCheckpointBytes};
}

HybridRecovery::HybridRecovery(const carve::SignatureScanner& scanner, RecoveryPlan plan) noexcept
	: scanner_(&scanner), plan_(plan) {}

Result<HybridRecovery::FilesystemPass> HybridRecovery::mountFailure(Error error) const {
	if (plan_.mode == RecoveryMode::kFilesystemOnly) {
		return error;
	}
	return FilesystemPass{.accounting = {}, .entries = 0, .mounted = false, .nonConforming = false};
}

Result<HybridRecovery::FilesystemPass>
HybridRecovery::walkScope(RunScope& scope, fs::EntryVisitor& visitor) const {
	ByteAccounting accounting;
	AccountingVisitor tee{visitor, accounting};
	const auto walked = walkLayout(scope, tee);
	if (!walked.hasValue()) {
		return mountFailure(walked.error());
	}
	return FilesystemPass{
		.accounting = std::move(accounting),
		.entries = tee.reported(),
		.mounted = true,
		.nonConforming = walked.value().nonConformingVolume};
}

Result<HybridRecovery::FilesystemPass>
HybridRecovery::runFilesystemPass(RunScope& scope, fs::EntryVisitor& visitor) const {
	if (plan_.mode == RecoveryMode::kCarveOnly) {
		return FilesystemPass{
			.accounting = {},
			.entries = 0,
			.mounted = false,
			.nonConforming = false};
	}
	if (plan_.resumeFrom.has_value()) {
		DroppingVisitor sink;
		return walkScope(scope, sink);
	}
	return walkScope(scope, visitor);
}

std::vector<carve::ScanRegion>
HybridRecovery::carveRegions(const FilesystemPass& pass, std::uint64_t deviceSize) const {
	if (plan_.mode == RecoveryMode::kFilesystemOnly) {
		return {};
	}
	// An empty accounting yields one gap covering the device, which is exactly
	// what carve-only and an unmountable volume both want.
	const auto gaps = pass.accounting.gaps(deviceSize);
	return chunked(regionsFrom(gaps, plan_.resumeFrom.value_or(0)), plan_.checkpointBytes);
}

Result<HybridRecovery::ScanTotals> HybridRecovery::scanNextRegion(
	BlockDevice& device,
	carve::ScanRegion region,
	carve::CandidateVisitor& visitor,
	ScanTotals totals) const {
	const auto stats = scanner_->scanRegion(device, region, visitor);
	if (!stats.hasValue()) {
		return stats.error();
	}
	return ScanTotals{
		.candidates = totals.candidates + stats.value().candidateCount,
		.regions = totals.regions + 1,
		.complete = true};
}

Result<HybridRecovery::ScanTotals> HybridRecovery::scanRegions(
	BlockDevice& device,
	std::span<const carve::ScanRegion> regions,
	carve::CandidateVisitor& visitor,
	ScanProgress& progress) const {
	Result<ScanTotals> totals = ScanTotals{.candidates = 0, .regions = 0, .complete = true};
	for (const carve::ScanRegion& region : regions) {
		totals = scanNextRegion(device, region, visitor, totals.value());
		if (!totals.hasValue() || !progress.onScanned(region.offset + region.lengthBytes)) {
			break;
		}
	}
	return stoppedShort(totals, regions.size());
}

Result<HybridRecovery::ScanTotals>
HybridRecovery::stoppedShort(const Result<ScanTotals>& totals, std::size_t regions) {
	if (!totals.hasValue()) {
		return totals;
	}
	return ScanTotals{
		.candidates = totals.value().candidates,
		.regions = totals.value().regions,
		.complete = totals.value().regions == regions};
}

RecoveryStats HybridRecovery::statsOf(const FilesystemPass& pass, const ScanTotals& totals) {
	return RecoveryStats{
		.entriesReported = pass.entries,
		.candidatesReported = totals.candidates,
		.accountedBytes = pass.accounting.accountedBytes(),
		.regionsScanned = totals.regions,
		.regionsDropped = pass.accounting.droppedRegions(),
		.filesystemMounted = pass.mounted,
		.nonConformingVolume = pass.nonConforming,
		.scanComplete = totals.complete};
}

Result<RecoveryStats> HybridRecovery::run(
	RunScope& scope,
	fs::EntryVisitor& entries,
	carve::CandidateVisitor& candidates,
	ScanProgress& progress) const {
	const auto pass = runFilesystemPass(scope, entries);
	if (!pass.hasValue()) {
		return pass.error();
	}
	BlockDevice& device = scope.device();
	const auto regions = carveRegions(pass.value(), device.sizeInBytes());
	return scanRegions(device, regions, candidates, progress)
		.map([&pass](const ScanTotals& totals) { return statsOf(pass.value(), totals); });
}

} // namespace revenant::recovery
