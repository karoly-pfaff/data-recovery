// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "cli/RunOutcome.hpp"

namespace revenant::cli {

// Drives `revenant-undelete` from a whole argument vector, program name
// included:
//
//   revenant-undelete --source <image> --destination <directory>
//                     [--hybrid | --fs-only | --carve-only]
//                     [--session <directory>]
//
// Reports the run — or why it stopped — on stderr. False means the process
// should exit non-zero.
[[nodiscard]] RunOutcome runUndeleteCli(std::span<char* const> args);

} // namespace revenant::cli
