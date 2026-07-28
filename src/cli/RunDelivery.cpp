// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RunDelivery.hpp"

#include <cstdint>
#include <utility>

#include "cli/RecoveryRun.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/recovery/Arbitration.hpp"
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

[[nodiscard]] RunReport reportOf(
	const RunRequest& request,
	const Discovery& found,
	const recovery::ExtractionStats& extraction) {
	return RunReport{
		.discovery = found.stats,
		.winners = static_cast<std::uint64_t>(found.decided.winners.size()),
		.suppressed = found.decided.suppressed,
		.extraction = extraction,
		.delivery = request.delivery};
}

// What an interrupted run has to say: what it found, and that it has not
// finished looking. Nothing was decided and nothing was written.
[[nodiscard]] RunReport
incompleteReport(const RunRequest& request, const recovery::RecoveryStats& scanned) {
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
				.deduplicated = 0},
		.delivery = request.delivery};
}

// The last of the architecture's three steps, or a stop just before it.
[[nodiscard]] recovery::Extraction deliver(
	recovery::RecoverySink& sink,
	BlockDevice& device,
	const RunRequest& request,
	const Discovery& found) {
	if (request.delivery == Delivery::kPreview) {
		return sink.preview(found.decided.winners);
	}
	return sink.extract(found.decided.winners, device);
}

// What was recovered, from where, and whether the bytes are the bytes — the
// durable record a run leaves behind for whoever did not watch it happen.
[[nodiscard]] recovery::SessionManifest
manifestOf(const RunRequest& request, const Discovery& found, recovery::Extraction extraction) {
	return recovery::SessionManifest{
		.source = request.source,
		.destination = request.destination,
		.mode = request.mode,
		.winners = static_cast<std::uint64_t>(found.decided.winners.size()),
		.suppressed = found.decided.suppressed,
		.artifacts = std::move(extraction.artifacts),
		.unreadable = std::move(extraction.unreadable)};
}

// A run whose manifest could not be written is a run that cannot be audited,
// so it fails rather than leaving files nothing accounts for.
[[nodiscard]] Result<RunReport>
recorded(const RunRequest& request, const Discovery& found, recovery::Extraction extraction) {
	const auto stats = extraction.stats;
	const auto written =
		recovery::writeManifest(request.session, manifestOf(request, found, std::move(extraction)));
	if (!written.hasValue()) {
		return written.error();
	}
	return reportOf(request, found, stats);
}

} // namespace

Result<RunReport> decideAndDeliver(
	BlockDevice& device,
	recovery::RecoverySink& sink,
	const RunRequest& request,
	const recovery::RecoveryStats& scanned) {
	if (!scanned.scanComplete) {
		return incompleteReport(request, scanned);
	}
	auto decided = recovery::arbitrateIndex(request.session);
	if (!decided.hasValue()) {
		return decided.error();
	}
	const Discovery found{.stats = scanned, .decided = std::move(decided.value())};
	return recorded(request, found, deliver(sink, device, request, found));
}

} // namespace revenant::cli
