// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RunDelivery.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "cli/RecoveryRun.hpp"
#include "cli/RunDamage.hpp"
#include "cli/RunManifest.hpp"
#include "cli/RunOutcome.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BadRange.hpp"
#include "revenant/recovery/Arbitration.hpp"
#include "revenant/recovery/Checkpoint.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/Manifest.hpp"
#include "revenant/recovery/RecoverySink.hpp"
#include "revenant/recovery/RunScope.hpp"

namespace revenant::cli {

namespace {

[[nodiscard]] std::uint64_t totalBytes(std::span<const BadRange> damage) {
	std::uint64_t bytes = 0;
	for (const BadRange& range : damage) {
		bytes += range.lengthBytes;
	}
	return bytes;
}

[[nodiscard]] RunReport reportOf(
	const RunRequest& request,
	const Discovery& found,
	const recovery::ExtractionStats& extraction,
	std::span<const BadRange> damage) {
	return RunReport{
		.discovery = found.stats,
		.winners = static_cast<std::uint64_t>(found.decided.winners.size()),
		.suppressed = found.decided.suppressed,
		.extraction = extraction,
		.delivery = request.delivery,
		.unreadableBytes = totalBytes(damage)};
}

// What an interrupted run has to say: what it found, and that it has not
// finished looking. Nothing was decided and nothing was written — but what the
// device refused on the way is still reported, because the next run resumes
// from here and the damage it already met is a fact about the disk.
[[nodiscard]] RunReport incompleteReport(
	const RunRequest& request,
	const recovery::RecoveryStats& scanned,
	std::span<const BadRange> damage) {
	return RunReport{
		.discovery = scanned,
		.winners = 0,
		.suppressed = 0,
		.extraction =
			recovery::ExtractionStats{
				.filesWritten = 0,
				.bytesWritten = 0,
				.failed = 0,
				.renamed = 0,
				.deduplicated = 0,
				.degraded = 0},
		.delivery = request.delivery,
		.unreadableBytes = totalBytes(damage)};
}

// The last of the architecture's three steps, or a stop just before it.
[[nodiscard]] recovery::Extraction deliver(
	recovery::RecoverySink& sink,
	const DeliverySource& source,
	const RunRequest& request,
	const Discovery& found) {
	if (request.delivery == Delivery::kPreview) {
		return sink.preview(found.decided.winners);
	}
	return sink.extract(found.decided.winners, *source.device);
}

// A run whose manifest could not be written is a run that cannot be audited,
// so it fails rather than leaving files nothing accounts for.
[[nodiscard]] Result<RunReport> recorded(
	const RunRequest& request,
	const Discovery& found,
	const DeliverySource& source,
	recovery::Extraction extraction) {
	const auto stats = extraction.stats;
	const auto stoppedBy = extraction.stoppedBy;
	const auto written = recovery::writeManifest(
		request.session,
		finishedManifest(request, found, std::move(extraction), source));
	if (!written.hasValue()) {
		return written.error();
	}
	// The manifest first, the failure second. A run that stopped because the
	// destination filled up still has to leave a record of what it wrote — the
	// files are there either way, and files nothing accounts for are the one
	// outcome worse than stopping.
	if (stoppedBy.has_value()) {
		return stoppedBy.value();
	}
	return reportOf(request, found, stats, source.stack->badRanges());
}

// How far the scan got, in device bytes.
//
// A scan that failed hands back an error and no statistics — a `Result` holds
// one or the other — so the number comes from the checkpoint the scan wrote as
// it went, which is where the *next* run reads it from too. That keeps the
// manifest and the resume point agreeing about the same byte.
[[nodiscard]] std::uint64_t
scannedUpTo(const RunRequest& request, const Result<recovery::RecoveryStats>& scanned) {
	if (scanned.hasValue()) {
		return scanned.value().scannedUpTo;
	}
	const auto checkpoint = recovery::readCheckpoint(request.session);
	return checkpoint.hasValue() ? checkpoint.value().scanCursor : 0;
}

// The device offset a stop names, or zero when it names none.
//
// Only `kSourceLost` carries one: every other code puts whatever its own layer
// found useful in `Error::offset` — a block number, an inode number, the size of
// a buffer — and a manifest field documented as a device offset must not hold
// any of those. That mixing is what story-0604 took out of `unreadable`.
[[nodiscard]] std::uint64_t deviceOffsetOf(const Error& error) {
	return error.code == ErrorCode::kSourceLost ? error.offset : 0;
}

// How a stopped scan ended, in the manifest's words. An interrupt carries no
// error, so it is the one ending named here rather than read off one.
[[nodiscard]] Ending endingOf(const Result<recovery::RecoveryStats>& scanned) {
	if (scanned.hasValue()) {
		return Ending{.outcome = nameOf(RunOutcome::kStoppedResumable)};
	}
	return Ending{
		.outcome = nameOf(outcomeOf(scanned.error().code)),
		.stoppedAt = deviceOffsetOf(scanned.error())};
}

// A scan that never finished, recorded and then reported as what it was. The
// manifest is written before the failure is returned, for the same reason it is
// on a full destination: a run that leaves nothing accounting for what it did is
// the worse outcome.
[[nodiscard]] Result<RunReport> stopped(
	const RunRequest& request,
	const DeliverySource& source,
	const Result<recovery::RecoveryStats>& scanned) {
	const auto ending = endingOf(scanned);
	// A run that could not start produced nothing, so there is nothing to
	// account for and no manifest to write. `--fs-only` over a volume that will
	// not mount reaches here, which is why this is a branch rather than a
	// comment claiming the case cannot arise.
	if (ending.outcome == nameOf(RunOutcome::kCouldNotStart)) {
		return scanned.error();
	}
	const auto written =
		recordWithoutArtifacts(request, source, ending, scannedUpTo(request, scanned));
	if (!written.hasValue()) {
		return written.error();
	}
	if (!scanned.hasValue()) {
		return scanned.error();
	}
	return incompleteReport(request, scanned.value(), source.stack->badRanges());
}

// Extraction, bracketed by the two manifest writes.
//
// One before a byte is written and one after: extraction is what fills a
// destination, so the first happens while there is still room, and if the
// second cannot be assembled for want of it the rename never happens and the
// first one stands. A stale manifest that says the run was still going is a far
// better record than recovered files nothing accounts for (story-0605).
[[nodiscard]] Result<RunReport> delivered(
	const RunRequest& request,
	const Discovery& found,
	const DeliverySource& source,
	recovery::RecoverySink& sink) {
	const auto reserved = recordWithoutArtifacts(
		request,
		source,
		Ending{.outcome = kStillRunning},
		found.stats.scannedUpTo);
	if (!reserved.hasValue()) {
		return reserved.error();
	}
	return recorded(request, found, source, marked(deliver(sink, source, request, found), source));
}

} // namespace

DeliverySource DeliverySource::of(const SourceStack& stack, recovery::RunScope& scope) {
	return DeliverySource{
		.device = &scope.device(),
		.stack = &stack,
		.startBytes = scope.startBytes()};
}

Result<RunReport> decideAndDeliver(
	const DeliverySource& source,
	recovery::RecoverySink& sink,
	const RunRequest& request,
	const Result<recovery::RecoveryStats>& scanned) {
	if (!scanned.hasValue() || !scanned.value().scanComplete) {
		return stopped(request, source, scanned);
	}
	auto decided = recovery::arbitrateIndex(request.session);
	if (!decided.hasValue()) {
		return decided.error();
	}
	const Discovery found{.stats = scanned.value(), .decided = std::move(decided.value())};
	return delivered(request, found, source, sink);
}

} // namespace revenant::cli
