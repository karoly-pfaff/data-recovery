// SPDX-License-Identifier: GPL-3.0-or-later
#include "perf/Report.hpp"

#include <ios>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "perf/Benchmark.hpp"

namespace revenant::perf {

namespace {

// Enough digits that a 0.1% change is visible, few enough that a line still
// reads. Fixed rather than scientific: these are seconds and rates, and nobody
// wants to parse an exponent to see that a scan got slower.
constexpr int kDigits = 6;

[[nodiscard]] std::string number(double value) {
	std::ostringstream text;
	text.precision(kDigits);
	text << std::fixed << value;
	return text.str();
}

[[nodiscard]] std::string field(const std::string& name, const std::string& value) {
	return "\"" + name + "\": " + value;
}

[[nodiscard]] std::string quoted(const std::string& value) {
	return "\"" + value + "\"";
}

[[nodiscard]] std::string objectFor(const Measurement& one) {
	return "    {" + field("name", quoted(one.name)) + ", " + field("unit", quoted(one.unit)) +
		   ", " + field("median_seconds", number(one.timing.medianSeconds)) + ", " +
		   field("min_seconds", number(one.timing.minSeconds)) + ", " +
		   field("max_seconds", number(one.timing.maxSeconds)) + ", " +
		   field("spread", number(one.timing.spread)) + ", " +
		   field("work_units", number(one.workUnits)) + ", " + field("rate", number(one.rate)) +
		   "}";
}

[[nodiscard]] std::string lineFor(const Measurement& one) {
	return "  " + one.name + ": " + number(one.rate) + " " + one.unit + " (median " +
		   number(one.timing.medianSeconds) + "s, spread " + number(one.timing.spread) + ")";
}

} // namespace

std::string reportJson(std::span<const Measurement> measured) {
	std::string body;
	for (const Measurement& one : measured) {
		body += body.empty() ? "" : ",\n";
		body += objectFor(one);
	}
	return "{\n  \"benchmarks\": [\n" + body + "\n  ]\n}\n";
}

std::vector<std::string> reportLines(std::span<const Measurement> measured) {
	std::vector<std::string> lines;
	lines.reserve(measured.size());
	for (const Measurement& one : measured) {
		lines.push_back(lineFor(one));
	}
	return lines;
}

std::string buildWarning() {
#ifdef NDEBUG
	return {};
#else
	return "warning: this build is not optimized; these timings are not comparable"
		   " with anything. Build the `release` preset before quoting a number.";
#endif
}

} // namespace revenant::perf
