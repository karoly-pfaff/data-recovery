// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RunDelivery.hpp"

#include <cstdint>
#include <span>
#include <utility>

#include "cli/RecoveryRun.hpp"
#include "recovery/Damage.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BadRange.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Arbitration.hpp"
#include "revenant/recovery/ArtifactRecord.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/Manifest.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::cli {

namespace {

// What discovery produced: the run's own statistics, and the candidates that
// survived arbitration.
struct Discovery {
	recovery::RecoveryStats stats{};
	recovery::Arbitration decided;
};

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

// Which of each artifact's bytes the run had to invent, written onto the records
// that are about to become the manifest.
//
// It happens here, and only here, because here is where the finished extraction
// and the stack that did the reading meet. A preview is marked too: the overlap
// is a fact about extents, not about whether anything was written.
[[nodiscard]] recovery::Extraction
marked(recovery::Extraction extraction, const DeliverySource& source) {
	const auto damage = source.stack->badRanges();
	if (damage.empty()) {
		return extraction;
	}
	for (recovery::ArtifactRecord& artifact : extraction.artifacts) {
		artifact.invented = recovery::inventedIn(artifact.extents, damage, source.startBytes);
		extraction.stats.degraded += artifact.invented.empty() ? 0U : 1U;
	}
	return extraction;
}

// Every artifact restated on the device the operator handed over.
//
// A scoped run records extents relative to its window, while the bad-sector map
// is device-absolute — and a document whose two range fields count from
// different origins is one an operator cannot compare against anything, least
// of all against itself. The manifest is one coordinate system, and it is the
// disk's. For a whole-source run the offset is zero and nothing moves.
[[nodiscard]] std::vector<recovery::ArtifactRecord>
onTheDevice(std::vector<recovery::ArtifactRecord> artifacts, std::uint64_t startBytes) {
	for (recovery::ArtifactRecord& artifact : artifacts) {
		for (fs::Extent& extent : artifact.extents) {
			extent.deviceOffset += startBytes;
		}
	}
	return artifacts;
}

// What was recovered, from where, and whether the bytes are the bytes — the
// durable record a run leaves behind for whoever did not watch it happen.
[[nodiscard]] recovery::SessionManifest manifestOf(
	const RunRequest& request,
	const Discovery& found,
	recovery::Extraction extraction,
	const DeliverySource& source) {
	const auto damage = source.stack->badRanges();
	return recovery::SessionManifest{
		.source = request.source,
		.destination = request.destination,
		.mode = request.mode,
		.winners = static_cast<std::uint64_t>(found.decided.winners.size()),
		.suppressed = found.decided.suppressed,
		.artifacts = onTheDevice(std::move(extraction.artifacts), source.startBytes),
		.unreadable = {damage.begin(), damage.end()}};
}

// A run whose manifest could not be written is a run that cannot be audited,
// so it fails rather than leaving files nothing accounts for.
[[nodiscard]] Result<RunReport> recorded(
	const RunRequest& request,
	const Discovery& found,
	const DeliverySource& source,
	recovery::Extraction extraction) {
	const auto stats = extraction.stats;
	const auto written = recovery::writeManifest(
		request.session,
		manifestOf(request, found, std::move(extraction), source));
	if (!written.hasValue()) {
		return written.error();
	}
	return reportOf(request, found, stats, source.stack->badRanges());
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
	const recovery::RecoveryStats& scanned) {
	if (!scanned.scanComplete) {
		return incompleteReport(request, scanned, source.stack->badRanges());
	}
	auto decided = recovery::arbitrateIndex(request.session);
	if (!decided.hasValue()) {
		return decided.error();
	}
	const Discovery found{.stats = scanned, .decided = std::move(decided.value())};
	return recorded(request, found, source, marked(deliver(sink, source, request, found), source));
}

} // namespace revenant::cli
