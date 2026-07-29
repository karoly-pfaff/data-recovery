// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "perf/Statistics.hpp"

namespace revenant::perf {

// How many repetitions a benchmark is timed over unless asked otherwise. Enough
// that a median means something; few enough that the whole suite finishes while
// someone is still looking at it.
inline constexpr unsigned kDefaultRepetitions = 5;

// One measured thing. `body` returns how much work it did — bytes, entries,
// candidates — so the rate is the benchmark's own arithmetic rather than the
// harness guessing at what it processed.
//
// A plain function pointer rather than std::function: every benchmark is a free
// function, and the CLI's own grammar tables use the same shape.
using BenchmarkBody = std::uint64_t (*)();

struct Benchmark {
	std::string_view name;
	// What one work unit is, per second: "MiB/s", "entries/s", "candidates/s".
	std::string_view unit;
	BenchmarkBody body;
};

// One benchmark's result: what it was, how long it took, and how fast that made
// it.
struct Measurement {
	std::string name;
	std::string unit;
	Statistics timing;
	double workUnits = 0;
	double rate = 0;
};

// Runs `benchmark` once untimed to warm it, then `repetitions` times, and
// measures those. The body's own return value is taken from the last
// repetition: every repetition does the same work, and a body whose work varies
// run to run is not a benchmark.
[[nodiscard]] Measurement measure(const Benchmark& benchmark, unsigned repetitions);

// Every benchmark whose name contains `filter`; all of them when it is empty.
[[nodiscard]] std::vector<Measurement>
measureAll(std::span<const Benchmark> benchmarks, std::string_view filter, unsigned repetitions);

// The suite this build carries.
[[nodiscard]] std::span<const Benchmark> allBenchmarks();

} // namespace revenant::perf
