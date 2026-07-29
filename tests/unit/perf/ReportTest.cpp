// SPDX-License-Identifier: GPL-3.0-or-later
// story-0050: what the harness writes down. The JSON is not decoration — it is
// the input `compare_baseline.py` gates on — so every field the gate reads is
// asserted to be there.
#include "perf/Report.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "perf/Benchmark.hpp"
#include "perf/Statistics.hpp"

namespace {

using revenant::perf::buildWarning;
using revenant::perf::Measurement;
using revenant::perf::reportJson;
using revenant::perf::reportLines;
using revenant::perf::statisticsOf;

[[nodiscard]] Measurement scanResult() {
	return Measurement{
		.name = "scan-throughput",
		.unit = "MiB/s",
		.timing = statisticsOf({0.5, 0.4, 0.6}),
		.workUnits = 32.0,
		.rate = 64.0};
}

[[nodiscard]] bool mentions(const std::string& text, const std::string& fragment) {
	return text.find(fragment) != std::string::npos;
}

TEST(Report, CarriesEveryFieldTheGateReads) {
	const std::vector<Measurement> measured{scanResult()};
	const auto json = reportJson(measured);
	EXPECT_TRUE(mentions(json, "\"name\": \"scan-throughput\""));
	EXPECT_TRUE(mentions(json, "\"unit\": \"MiB/s\""));
	EXPECT_TRUE(mentions(json, "\"rate\""));
	EXPECT_TRUE(mentions(json, "\"spread\""));
	EXPECT_TRUE(mentions(json, "\"work_units\""));
}

TEST(Report, SeparatesBenchmarksSoTheDocumentParses) {
	const std::vector<Measurement> measured{scanResult(), scanResult()};
	const auto json = reportJson(measured);
	EXPECT_TRUE(mentions(json, "},\n"));
	EXPECT_TRUE(mentions(json, "\"benchmarks\": ["));
}

// A run whose filter matched nothing still has to produce a document.
TEST(Report, AnEmptyRunIsStillADocument) {
	const auto json = reportJson({});
	EXPECT_TRUE(mentions(json, "\"benchmarks\""));
}

TEST(Report, NamesTheBenchmarkAndItsUnitForAPerson) {
	const std::vector<Measurement> measured{scanResult()};
	const auto lines = reportLines(measured);
	ASSERT_EQ(lines.size(), 1U);
	EXPECT_TRUE(mentions(lines.front(), "scan-throughput"));
	EXPECT_TRUE(mentions(lines.front(), "MiB/s"));
	EXPECT_TRUE(mentions(lines.front(), "spread"));
}

// This test binary is built with sanitizers and without NDEBUG, so the warning
// must be present — which is exactly the case it exists for.
TEST(Report, AnUnoptimizedBuildSaysItsNumbersAreNotComparable) {
#ifdef NDEBUG
	EXPECT_TRUE(buildWarning().empty());
#else
	EXPECT_TRUE(mentions(buildWarning(), "not optimized"));
#endif
}

} // namespace
