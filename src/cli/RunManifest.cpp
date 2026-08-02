// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RunManifest.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "cli/RunDamage.hpp"
#include "cli/RunOutcome.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/SourceStack.hpp"
#include "revenant/recovery/Manifest.hpp"

namespace revenant::cli {

std::string_view nameOf(RunOutcome outcome) {
	switch (outcome) {
	case RunOutcome::kFinished:
		return "finished";
	case RunOutcome::kStoppedResumable:
		return "stopped-resumable";
	case RunOutcome::kStoppedNeedsAttention:
		return "stopped-needs-attention";
	case RunOutcome::kCouldNotStart:
	case RunOutcome::kUsageError:
		break;
	}
	// Neither reaches here: a run that never started writes no manifest.
	return "did-not-start";
}

Result<std::filesystem::path> recordWithoutArtifacts(
	const RunRequest& request,
	const DeliverySource& source,
	const Ending& ending,
	const recovery::RecoveryStats& scanned) {
	const auto damage = source.stack->badRanges();
	return recovery::writeManifest(
		request.session,
		recovery::SessionManifest{
			.source = request.source,
			.destination = request.destination,
			.mode = request.mode,
			.winners = 0,
			.suppressed = 0,
			.artifacts = {},
			.outcome = std::string{ending.outcome},
			.scannedUpTo = scanned.scannedUpTo,
			.stoppedAt = ending.stoppedAt,
			.unreadable = {damage.begin(), damage.end()}});
}

recovery::SessionManifest finishedManifest(
	const RunRequest& request,
	const Discovery& found,
	recovery::Extraction extraction,
	const DeliverySource& source) {
	const auto ending = extraction.stoppedBy.has_value()
		? outcomeOf(extraction.stoppedBy.value().code)
		: RunOutcome::kFinished;
	const auto damage = source.stack->badRanges();
	return recovery::SessionManifest{
		.source = request.source,
		.destination = request.destination,
		.mode = request.mode,
		.winners = static_cast<std::uint64_t>(found.decided.winners.size()),
		.suppressed = found.decided.suppressed,
		.artifacts = onTheDevice(std::move(extraction.artifacts), source.startBytes),
		.outcome = std::string{nameOf(ending)},
		.scannedUpTo = found.stats.scannedUpTo,
		.stoppedAt = 0,
		.unreadable = {damage.begin(), damage.end()}};
}

} // namespace revenant::cli
