// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace revenant::cli {

// Ctrl-C means "stop cleanly", not "abort": a recovery that dies mid-scan
// leaves its session unusable, and re-reading a dying disk from zero is exactly
// what ADR-0008 exists to avoid. The handler only raises a flag; the run
// finishes the chunk it is on, checkpoints, and stops.
//
// A second interrupt is left to the default handler, so an operator who really
// means it is never trapped. Installing the handler starts a fresh watch: what
// interrupted a previous run does not stop this one.
void catchInterrupts();

// Whether an interrupt has been seen since `catchInterrupts` was called.
[[nodiscard]] bool interrupted() noexcept;

} // namespace revenant::cli
