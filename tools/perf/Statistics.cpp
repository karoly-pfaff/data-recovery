// SPDX-License-Identifier: GPL-3.0-or-later
#include "perf/Statistics.hpp"

#include <algorithm>
#include <vector>

namespace revenant::perf {

namespace {

// The middle of an already-sorted set; the mean of the middle two when there is
// no single middle.
[[nodiscard]] double medianOfSorted(const std::vector<double>& sorted) {
	const auto count = sorted.size();
	const auto middle = count / 2;
	if (count % 2 == 1) {
		return sorted.at(middle);
	}
	return (sorted.at(middle - 1) + sorted.at(middle)) / 2.0;
}

[[nodiscard]] double spreadOf(const std::vector<double>& sorted, double median) {
	if (median <= 0.0) {
		return 0.0;
	}
	return (sorted.back() - sorted.front()) / median;
}

} // namespace

Statistics statisticsOf(std::vector<double> seconds) {
	if (seconds.empty()) {
		return Statistics{};
	}
	std::ranges::sort(seconds);
	const auto median = medianOfSorted(seconds);
	return Statistics{
		.medianSeconds = median,
		.minSeconds = seconds.front(),
		.maxSeconds = seconds.back(),
		.spread = spreadOf(seconds, median)};
}

double ratePerSecond(double workUnits, const Statistics& timing) {
	if (timing.medianSeconds <= 0.0) {
		return 0.0;
	}
	return workUnits / timing.medianSeconds;
}

} // namespace revenant::perf
