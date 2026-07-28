// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

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
[[nodiscard]] bool runCarveCli(std::span<char* const> args);

} // namespace revenant::cli
