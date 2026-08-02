// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

#include "cli/RecoveryOptions.hpp"
#include "cli/RecoveryRun.hpp"
#include "cli/RunOutcome.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::cli {

// A frontend's grammar: the arguments after the program name, parsed into the
// run they describe.
using Grammar = Result<RunRequest> (*)(Arguments);

// Everything a recovery frontend does that is not its own — `--help`, the
// parse, the run, and the words for what happened — over a whole argument
// vector, program name included. Two binaries over one engine differ in their
// flags and their usage text; a second copy of this would be a second place for
// them to drift.
//
// Reports on stderr; the value it returns is the process's exit status, which
// says what the caller should do next rather than merely that something went
// wrong (story-0605).
[[nodiscard]] RunOutcome
runFrontend(std::span<char* const> args, std::string_view usage, Grammar grammar);

} // namespace revenant::cli
