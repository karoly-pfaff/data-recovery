// SPDX-License-Identifier: GPL-3.0-or-later
#include "perf/BenchMain.hpp"

#include <charconv>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "perf/Benchmark.hpp"
#include "perf/Report.hpp"

namespace revenant::perf {

namespace {

constexpr std::string_view kJsonFlag = "--json";
constexpr std::string_view kFilterFlag = "--filter";
constexpr std::string_view kRepetitionsFlag = "--repetitions";

// What a command line asked for. Every field has a usable default, so an
// argument list of nothing at all runs the whole suite.
struct BenchOptions {
	std::string jsonPath;
	std::string filter;
	unsigned repetitions = kDefaultRepetitions;
};

// The index bounds are checked by the caller before it reads; std::span has no
// checked accessor in C++20.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
[[nodiscard]] std::string_view argAt(std::span<char* const> args, std::size_t index) {
	return args[index];
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// std::from_chars's [first, last) pointer pair is the only overload portable
// across our toolchains; the arithmetic spans one already-bounded string_view.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
[[nodiscard]] unsigned repetitionsIn(std::string_view text, unsigned fallback) {
	unsigned value = 0;
	const auto [end, failure] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (failure != std::errc{} || end != text.data() + text.size() || value == 0) {
		return fallback;
	}
	return value;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

// A flag and the value after it. Named rather than passed as two adjacent
// string_views, which is exactly the pair a caller can swap without the compiler
// noticing.
struct Flag {
	std::string_view name;
	std::string_view value;
};

void applyFlag(BenchOptions& options, const Flag& flag) {
	if (flag.name == kJsonFlag) {
		options.jsonPath = flag.value;
	}
	if (flag.name == kFilterFlag) {
		options.filter = flag.value;
	}
	if (flag.name == kRepetitionsFlag) {
		options.repetitions = repetitionsIn(flag.value, options.repetitions);
	}
}

// Flags come in pairs, and an odd one at the end names no value, so it is
// ignored rather than read past the end of the vector.
[[nodiscard]] BenchOptions optionsIn(std::span<char* const> args) {
	BenchOptions options;
	for (std::size_t at = 1; at + 1 < args.size(); at += 2) {
		applyFlag(options, Flag{.name = argAt(args, at), .value = argAt(args, at + 1)});
	}
	return options;
}

void writeJson(const std::string& path, std::span<const Measurement> measured) {
	if (path.empty()) {
		return;
	}
	std::ofstream out{path};
	out << reportJson(measured);
}

void printLines(const std::vector<std::string>& lines) {
	for (const std::string& line : lines) {
		std::puts(line.c_str());
	}
}

} // namespace

int runBenchmarks(std::span<char* const> args) {
	const auto options = optionsIn(args);
	const auto warning = buildWarning();
	if (!warning.empty()) {
		std::puts(warning.c_str());
	}
	const auto measured = measureAll(allBenchmarks(), options.filter, options.repetitions);
	printLines(reportLines(measured));
	writeJson(options.jsonPath, measured);
	return 0;
}

} // namespace revenant::perf
