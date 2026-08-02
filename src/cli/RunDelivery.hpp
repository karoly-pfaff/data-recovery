// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cli/RecoveryRun.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/SourceStack.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/RecoverySink.hpp"
#include "revenant/recovery/RunScope.hpp"

namespace revenant::cli {

// Where a delivered artifact's bytes came from: the device extraction reads
// them back through, the stack that did the reading, and where the first of
// those two sits on the second.
//
// The three travel together because marking an artifact as degraded needs all
// of them. `device` is the run's own range — a window for a scoped run — and
// its extents are relative to it, while `stack.badRanges()` is device-absolute;
// `startBytes` is what lines the two up. The map is asked for late, after
// extraction has done its reading, because reading is what adds to it.
struct DeliverySource {
	// Assembled from the two things that know: the stack that did the reading,
	// and the scope that says where this run's zero sits on it. A named
	// constructor rather than three fields filled at the call site, because
	// getting `startBytes` wrong is silent — the damage simply stops
	// intersecting anything — and a test can only catch that if it builds the
	// value the same way production does.
	[[nodiscard]] static DeliverySource of(const SourceStack& stack, recovery::RunScope& scope);

	BlockDevice* device;      // non-owning, never null
	const SourceStack* stack; // non-owning, never null
	std::uint64_t startBytes;
};

// Everything a run does once its scan is over: arbitrate the index, write (or
// preview) the winners, and record what happened. Split from the run itself
// because getting a scan finished and deciding what to do with a finished one
// are two jobs.
//
// An interrupted scan stops here and stays stopped. Arbitrating a partial index
// can crown a winner the finished scan would have suppressed — the candidate
// that beats it is in the tail nobody has read — so extracting from it would
// write files a complete run never would. What is on disk is what the next run
// resumes from.
[[nodiscard]] Result<RunReport> decideAndDeliver(
	const DeliverySource& source,
	recovery::RecoverySink& sink,
	const RunRequest& request,
	const Result<recovery::RecoveryStats>& scanned);

} // namespace revenant::cli
