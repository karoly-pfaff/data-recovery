// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

namespace revenant::perf {

// What a set of repetitions says about a benchmark. The median is the headline:
// a mean is dragged around by the one repetition that happened to land while
// something else ran on the machine. The spread is the veto — it says whether
// the median is worth believing at all.
struct Statistics {
	double medianSeconds = 0;
	double minSeconds = 0;
	double maxSeconds = 0;
	// (max - min) / median. Relative, because a 2 ms swing means something very
	// different on a 5 ms benchmark than on a 5 s one. Zero when there is one
	// sample, or none.
	double spread = 0;
};

// The statistics of `seconds`, which it takes by value because it sorts them.
// An empty set is a zero statistic rather than undefined behaviour: a benchmark
// that ran nothing has no timing, and that is a number the report can print.
[[nodiscard]] Statistics statisticsOf(std::vector<double> seconds);

// `workUnits` per second at the median timing — the rate every benchmark's
// primary metric is expressed in. Zero when nothing was timed, because a rate
// over no time is not infinity, it is unmeasured.
[[nodiscard]] double ratePerSecond(double workUnits, const Statistics& timing);

} // namespace revenant::perf
