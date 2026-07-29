// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string>
#include <vector>

#include "perf/Benchmark.hpp"

namespace revenant::perf {

// The results as JSON — what `compare_baseline.py` reads, and what CI keeps as
// the artifact a later run is compared against. One object per benchmark, with
// the spread carried alongside the median because the comparison needs both.
[[nodiscard]] std::string reportJson(std::span<const Measurement> measured);

// The same results for a person: one line per benchmark, naming its rate and
// how much the repetitions disagreed.
[[nodiscard]] std::vector<std::string> reportLines(std::span<const Measurement> measured);

// The line a build that cannot be believed prints first. Empty when this build
// was optimized: a Debug or sanitized binary produces timings that mean nothing,
// and someone will otherwise paste them into a pull request.
[[nodiscard]] std::string buildWarning();

} // namespace revenant::perf
