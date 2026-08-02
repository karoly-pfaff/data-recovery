// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. What a run writes down, in the two shapes a run can leave: one that
// reached extraction and has artifacts to list, and one that stopped before it
// and has only a stop to record. Split from the sequencing that calls it
// because deciding what to write down and deciding what to do next are two
// jobs. Not a public interface.

#include <cstdint>
#include <filesystem>
#include <string_view>

#include "cli/RecoveryRun.hpp"
#include "cli/RunDelivery.hpp"
#include "cli/RunOutcome.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/Arbitration.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/Manifest.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::cli {

// What discovery produced: the run's own statistics, and the candidates that
// survived arbitration.
struct Discovery {
	recovery::RecoveryStats stats{};
	recovery::Arbitration decided;
};

// A run in the middle of extracting. Not an exit status — no run ends here —
// which is why it is a name rather than a `RunOutcome`.
inline constexpr std::string_view kStillRunning = "in-progress";

// How a run ended, as the manifest states it.
struct Ending {
	std::string_view outcome;
	// The device offset the stop itself names — where a lost source stopped
	// answering. Zero when the stop names none.
	std::uint64_t stoppedAt = 0;
};

// The words the exit-code table uses, so the manifest and the exit status
// cannot disagree about how the run ended.
[[nodiscard]] std::string_view nameOf(RunOutcome outcome);

// The record a run leaves when it has no artifacts to list: how far it got, why
// it stopped, and what the device refused on the way.
//
// No winners and no artifacts, deliberately. Arbitrating a partial index can
// crown a winner the finished scan would have suppressed — the candidate that
// beats it may be in the tail nobody has read — so a stopped run decides
// nothing. What it leaves is a record of the stop, which is what the next run
// and whoever reads it afterwards both need.
//
// It is also what a run writes *before* extraction begins, under the outcome
// `in-progress`: extraction is what fills a destination, so that write happens
// while there is still room, and if the real one cannot be assembled later the
// rename never happens and this one stands.
[[nodiscard]] Result<std::filesystem::path> recordWithoutArtifacts(
	const RunRequest& request,
	const DeliverySource& source,
	const Ending& ending,
	const recovery::RecoveryStats& scanned);

// The record a run leaves once extraction is over — what was recovered, from
// where, and whether the bytes are the bytes. How it ended is read off the
// extraction itself, which is the only thing that knows.
[[nodiscard]] recovery::SessionManifest finishedManifest(
	const RunRequest& request,
	const Discovery& found,
	recovery::Extraction extraction,
	const DeliverySource& source);

} // namespace revenant::cli
