// SPDX-License-Identifier: GPL-3.0-or-later
#include "perf/Benchmark.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "perf/Statistics.hpp"

namespace revenant::perf {

namespace {

// One repetition, timed. The steady clock rather than the system one: this
// measures an interval, and the system clock is allowed to step sideways.
[[nodiscard]] double secondsFor(BenchmarkBody body, std::uint64_t& workUnits) {
	const auto started = std::chrono::steady_clock::now();
	workUnits = body();
	const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
	return elapsed.count();
}

// Every timing, warmed first. The first pass through a fixture touches pages
// nobody has touched and fills caches nobody has filled, and that cost lands on
// whichever repetition happens to be first â€” it showed up as a max 20% above the
// median until this run existed, and 1.4% after.
[[nodiscard]] std::vector<double>
timings(BenchmarkBody body, unsigned repetitions, std::uint64_t& workUnits) {
	std::vector<double> seconds;
	seconds.reserve(repetitions);
	static_cast<void>(body());
	for (unsigned run = 0; run < repetitions; ++run) {
		seconds.push_back(secondsFor(body, workUnits));
	}
	return seconds;
}

} // namespace

Measurement measure(const Benchmark& benchmark, unsigned repetitions) {
	std::uint64_t workUnits = 0;
	const auto timing = statisticsOf(timings(benchmark.body, repetitions, workUnits));
	return Measurement{
		.name = std::string{benchmark.name},
		.unit = std::string{benchmark.unit},
		.timing = timing,
		.workUnits = static_cast<double>(workUnits),
		.rate = ratePerSecond(static_cast<double>(workUnits), timing)};
}

std::vector<Measurement>
measureAll(std::span<const Benchmark> benchmarks, std::string_view filter, unsigned repetitions) {
	std::vector<Measurement> measured;
	for (const Benchmark& benchmark : benchmarks) {
		if (filter.empty() || benchmark.name.find(filter) != std::string_view::npos) {
			measured.push_back(measure(benchmark, repetitions));
		}
	}
	return measured;
}

} // namespace revenant::perf
