// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cli/RecoveryRun.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::cli {

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
	BlockDevice& device,
	recovery::RecoverySink& sink,
	const RunRequest& request,
	const recovery::RecoveryStats& scanned);

} // namespace revenant::cli
