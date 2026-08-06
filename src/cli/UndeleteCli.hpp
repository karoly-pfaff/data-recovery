// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string>

#include "cli/RunOutcome.hpp"

namespace revenant::cli {

// Drives `revenant-undelete` from a whole argument vector, program name
// included. The flags it takes are not restated here — `undeleteUsage()` below
// renders them from the table the parser reads (story-0702).
//
// Reports the run — or why it stopped — on stderr. False means the process
// should exit non-zero.
[[nodiscard]] RunOutcome runUndeleteCli(std::span<char* const> args);

// What `--help` prints: the synopsis, then the flag list rendered from the
// table the parser reads. Exposed so a test can observe that the rendering is
// wired in at all (story-0702).
[[nodiscard]] std::string undeleteUsage();

} // namespace revenant::cli
