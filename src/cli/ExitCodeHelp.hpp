// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The exit-code table, as both frontends print it under `--help`.
//
// One copy, because the two usage texts differ in their flags and not in what
// their exit status means — and because a table stated twice is a table that
// drifts. The numbers themselves live in `RunOutcome`; this is their prose.

#include <string_view>

namespace revenant::cli {

inline constexpr std::string_view kExitCodes =
	"exit codes:\n"
	"  0  finished; the manifest is written. Per-artifact failures are in it\n"
	"  1  could not start; nothing was produced\n"
	"  2  the arguments were refused\n"
	"  3  stopped early; re-run the same command to carry on\n"
	"  4  stopped early; something needs attention before a re-run gets further";

} // namespace revenant::cli
