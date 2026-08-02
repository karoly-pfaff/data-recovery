// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RunDelivery.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cli/RecoveryRun.hpp"
#include "cli/RunOutcome.hpp"
#include "cli/RunDamage.hpp"
#include "cli/RunManifest.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BadRange.hpp"
#include "revenant/recovery/Arbitration.hpp"
#include "revenant/recovery/ArtifactRecord.hpp"
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

// What a run that never reached extraction still owes: how far it got, why it
// stopped, and what the device refused on the way.
//
// No winners and no artifacts, deliberately. Arbitrating a partial index can
// crown a winner the finished scan would have suppressed — the candidate that
// beats it may be in the tail nobody has read — so a stopped run decides
// nothing. What it leaves is a record of the stop, which is what the next run
// and whoever reads it afterwards both need.
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

// A scan that never finished, recorded and then reported as what it was. The
// manifest is written first for the same reason it is on a full destination: a
// run that leaves nothing accounting for what it did is the worse outcome.
[[nodiscard]] Result<RunReport> stopped(
	const RunRequest& request,
	const DeliverySource& source,
	const Result<recovery::RecoveryStats>& scanned) {
	const auto ending = scanned.hasValue()
		? RunOutcome::kStoppedResumable
		: outcomeOf(scanned.error().code);
	const auto stats = scanned.hasValue() ? scanned.value() : recovery::RecoveryStats{};
	const auto stoppedAt = scanned.hasValue() ? 0 : scanned.error().offset;
	const auto written = recordWithoutArtifacts(
		request,
		source,
		Ending{.outcome = nameOf(ending), .stoppedAt = stoppedAt},
		stats);
	if (!written.hasValue()) {
		return written.error();
	}
	if (!scanned.hasValue()) {
		return scanned.error();
	}
	return incompleteReport(request, stats, source.stack->badRanges());
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
	// A manifest before a byte is written, and the real one after. Extraction is
	// what fills a destination, so the first write happens while there is still
	// room — and if the second cannot be assembled for want of it, the rename
	// never happens and the first one stands. A stale manifest that says the run
	// was still going is a far better record than recovered files nothing
	// accounts for (story-0605).
	const auto reserved = recordWithoutArtifacts(
		request,
		source,
		Ending{.outcome = kStillRunning},
		scanned.value());
	if (!reserved.hasValue()) {
		return reserved.error();
	}
	return recorded(request, found, source, marked(deliver(sink, source, request, found), source));
}

} // namespace revenant::cli
