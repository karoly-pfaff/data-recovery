// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string>

#include "cli/RunOutcome.hpp"

namespace revenant::cli {

// Drives `revenant-carve` from a whole argument vector, program name included:
//
//   revenant-carve --source <image> --destination <directory>
//                  [--formats <ext,ext,…>]
//                  [--session <directory>]
//
// Recovers by structure alone — no filesystem is consulted, so nothing comes
// back with a name. Reports the run, or why it stopped, on stderr; false means
// the process should exit non-zero.
[[nodiscard]] RunOutcome runCarveCli(std::span<char* const> args);

// What `--help` prints: the synopsis, the formats this build carves, and the
// flag list rendered from the table the parser reads. Exposed so a test can
// observe that the rendering is wired in at all (story-0702).
[[nodiscard]] std::string carveUsage();

} // namespace revenant::cli
