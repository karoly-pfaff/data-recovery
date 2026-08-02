// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. What a run leaves its caller to do next, and the exit status that
// says so. Not a public interface — but the *numbers* are: exit codes are CLI
// behaviour, and CLI behaviour freezes at 1.0 (docs/versioning.md), so this
// enum is the one place they are decided.

#include <cstdint>

#include "revenant/core/Error.hpp"

namespace revenant::cli {

// The code answers "what should the caller do next"; stderr and the manifest
// answer "what happened". A code per cause was considered and rejected: it
// would freeze a new integer for every future way to die, and scripts branch on
// what-next, not on which component failed.
enum class RunOutcome : std::uint8_t {
	// The scan finished and the manifest was written. Per-artifact failures are
	// recorded in it, not hidden — a finished run is not a perfect one.
	kFinished = 0,
	// Nothing was produced: the source or the destination was refused before
	// any work happened.
	kCouldNotStart = 1,
	// The grammar refused the arguments.
	kUsageError = 2,
	// Stopped early, and re-running the same command carries on: an interrupt,
	// or a source that went away. Nothing needs fixing first.
	kStoppedResumable = 3,
	// Stopped early, and something needs attention before a re-run would get
	// further — a full destination, a session directory that stopped taking
	// writes, or a fault the run could not classify.
	kStoppedNeedsAttention = 4,
};

// Which of the five a failure is. The enum is listed exhaustively so that
// adding an `ErrorCode` is a compile error until it has been given an answer.
[[nodiscard]] RunOutcome outcomeOf(ErrorCode code) noexcept;

} // namespace revenant::cli
