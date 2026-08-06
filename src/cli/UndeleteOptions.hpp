// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cli/RecoveryOptions.hpp"
#include "cli/RecoveryRun.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::cli {

// Parses `revenant-undelete`'s arguments — everything after the program name,
// against `undeleteFlags()`. Which flags those are is the table's answer, not
// this comment's (story-0702).
//
// Both paths are required and the mode defaults to hybrid. A second mode flag
// is refused rather than resolved: two contradictory instructions are not a
// refinement of one, and guessing which was meant is exactly the silent wrong
// thing a recovery tool must not do.
[[nodiscard]] Result<RunRequest> parseUndeleteOptions(Arguments arguments);

} // namespace revenant::cli
