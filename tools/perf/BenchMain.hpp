// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

namespace revenant::perf {

// Runs the suite the way `main` hands it a command line — program name
// included. Logic lives here rather than in main() so that a benchmark harness
// is testable code like everything else in this tree.
//
//   revenant-bench [--filter <substring>] [--repetitions <n>] [--json <path>]
//
// Always succeeds: a benchmark reports a number, it does not pass or fail.
// Deciding whether that number is acceptable is `compare_baseline.py`'s job.
[[nodiscard]] int runBenchmarks(std::span<char* const> args);

} // namespace revenant::perf
