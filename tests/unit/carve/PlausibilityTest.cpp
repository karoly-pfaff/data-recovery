// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/Plausibility.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "revenant/carve/CarveResult.hpp"
#include "revenant/core/Confidence.hpp"

namespace {

using revenant::Confidence;
using revenant::carve::applyPlausibility;
using revenant::carve::CarveResult;
using revenant::carve::plausibleMinimumBytes;

[[nodiscard]] CarveResult resultOf(std::uint64_t length, Confidence confidence) {
	return {.length = length, .confidence = confidence, .extension = "jpg"};
}

// The case the filter exists for: 22 bytes can be a structurally perfect JPEG,
// and a disk full of random data will eventually produce one.
TEST(Plausibility, ATinyButStructurallyValidMatchIsRejected) {
	const auto filtered = applyPlausibility(resultOf(22, Confidence::kValid));
	EXPECT_EQ(filtered.confidence, Confidence::kRejected);
	EXPECT_EQ(filtered.length, 0U);
	EXPECT_EQ(filtered.extension, "jpg");
}

TEST(Plausibility, AResultAtTheFloorSurvivesUntouched) {
	const auto floor = plausibleMinimumBytes("jpg");
	const auto filtered = applyPlausibility(resultOf(floor, Confidence::kValid));
	EXPECT_EQ(filtered.confidence, Confidence::kValid);
	EXPECT_EQ(filtered.length, floor);
}

TEST(Plausibility, AComfortablyLargeResultSurvivesUntouched) {
	const auto filtered = applyPlausibility(resultOf(1U << 20U, Confidence::kValid));
	EXPECT_EQ(filtered.confidence, Confidence::kValid);
	EXPECT_EQ(filtered.length, 1U << 20U);
}

TEST(Plausibility, AnUncertainResultBelowTheFloorIsAlsoRejected) {
	const auto filtered = applyPlausibility(resultOf(10, Confidence::kUncertain));
	EXPECT_EQ(filtered.confidence, Confidence::kRejected);
	EXPECT_EQ(filtered.length, 0U);
}

TEST(Plausibility, AnAlreadyRejectedResultIsLeftAlone) {
	const auto filtered = applyPlausibility(resultOf(0, Confidence::kRejected));
	EXPECT_EQ(filtered.confidence, Confidence::kRejected);
	EXPECT_EQ(filtered.length, 0U);
}

TEST(Plausibility, TheFilterNeverUpgradesAVerdict) {
	const auto filtered = applyPlausibility(resultOf(1U << 20U, Confidence::kUncertain));
	EXPECT_EQ(filtered.confidence, Confidence::kUncertain);
}

TEST(Plausibility, EveryShippedFormatDeclaresItsOwnFloor) {
	EXPECT_GT(plausibleMinimumBytes("jpg"), plausibleMinimumBytes("png"));
	EXPECT_EQ(plausibleMinimumBytes("mp4"), plausibleMinimumBytes("mov"));
	EXPECT_EQ(plausibleMinimumBytes("docx"), plausibleMinimumBytes("zip"));
	EXPECT_GT(plausibleMinimumBytes("cr2"), 0U);
	EXPECT_GT(plausibleMinimumBytes("pdf"), 0U);
}

// Deliberate: a format this build knows nothing about gets no floor, because
// any number chosen for it would be invention rather than a format fact.
TEST(Plausibility, AnUnknownExtensionHasNoFloorAndIsLeftAlone) {
	EXPECT_EQ(plausibleMinimumBytes("wat"), 0U);
	const auto filtered = applyPlausibility(
		CarveResult{.length = 1, .confidence = Confidence::kValid, .extension = "wat"});
	EXPECT_EQ(filtered.confidence, Confidence::kValid);
	EXPECT_EQ(filtered.length, 1U);
}

} // namespace
