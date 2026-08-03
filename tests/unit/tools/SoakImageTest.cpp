// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/SoakImage.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "imagegen/PatternWriter.hpp"
#include "support/FixtureContent.hpp"

namespace {

using revenant::imagegen::kSectorBytes;
using revenant::imagegen::kSoakPlantBytes;
using revenant::imagegen::soakPlan;
using revenant::imagegen::soakPlanPath;
using revenant::imagegen::writeSoakImage;
using revenant::testing::readFileBytes;
using revenant::testing::readFileText;

// Big enough for four plants with filler between them, small enough that a unit
// test writes it in milliseconds.
constexpr std::uint64_t kTestImageBytes = 1U << 20;
constexpr std::uint64_t kTestPlants = 4;

// The two bytes every JPEG opens with; what a plant looks like from outside.
constexpr std::byte kSoi0{0xFF};
constexpr std::byte kSoi1{0xD8};

std::filesystem::path tempSoakImage(const std::string& name) {
	return std::filesystem::temp_directory_path() / ("revenant-soak-" + name + ".img");
}

void removeSoakImage(const std::filesystem::path& path) {
	std::filesystem::remove(path);
	std::filesystem::remove(soakPlanPath(path));
}

TEST(SoakImage, PlansEvenlySpacedSectorAlignedPlants) {
	const auto plan = soakPlan(kTestImageBytes, kTestPlants);
	ASSERT_EQ(plan.size(), kTestPlants);
	EXPECT_EQ(plan.front().offset, 0U);
	EXPECT_EQ(plan.at(1).offset, kTestImageBytes / kTestPlants);
	EXPECT_EQ(plan.at(1).offset % kSectorBytes, 0U);
	EXPECT_EQ(plan.back().length, kSoakPlantBytes);
}

// A plant that would overlap its neighbour is not a smaller plant; it is a
// request the fixture cannot honour, and honouring it silently would make the
// recorded offsets a lie.
TEST(SoakImage, RefusesPlantsThatWouldNotFit) {
	EXPECT_TRUE(soakPlan(kSoakPlantBytes, 2).empty());
	EXPECT_TRUE(soakPlan(kTestImageBytes, 0).empty());
}

TEST(SoakImage, WritesTheRequestedSize) {
	const auto path = tempSoakImage("size");
	ASSERT_EQ(writeSoakImage(path, kTestImageBytes, kTestPlants).value(), kTestImageBytes);
	EXPECT_EQ(std::filesystem::file_size(path), kTestImageBytes);
	removeSoakImage(path);
}

TEST(SoakImage, PlantsAJpegAtEveryOffsetItRecords) {
	const auto path = tempSoakImage("plants");
	ASSERT_TRUE(writeSoakImage(path, kTestImageBytes, kTestPlants).hasValue());
	const auto image = readFileBytes(path);
	ASSERT_EQ(image.size(), kTestImageBytes);
	for (const auto& plant : soakPlan(kTestImageBytes, kTestPlants)) {
		EXPECT_EQ(image.at(plant.offset), kSoi0);
		EXPECT_EQ(image.at(plant.offset + 1), kSoi1);
	}
	removeSoakImage(path);
}

// The gap between plants is the same counter filler the perf fixture is made
// of — not zeros, and not more of the plant in front of it. It witnesses that
// and no more: `kCounter` repeats every 256 bytes, so no assertion on these
// bytes can tell a device offset from a running write cursor.
TEST(SoakImage, TheGapBetweenPlantsIsCounterFiller) {
	const auto path = tempSoakImage("filler");
	ASSERT_TRUE(writeSoakImage(path, kTestImageBytes, kTestPlants).hasValue());
	const auto image = readFileBytes(path);
	const std::size_t inGap = kSoakPlantBytes + 300;
	EXPECT_EQ(image.at(inGap), static_cast<std::byte>(inGap & 0xFFU));
	EXPECT_EQ(image.at(inGap + 1), static_cast<std::byte>((inGap + 1) & 0xFFU));
	removeSoakImage(path);
}

// Two plants must be two files. The extractor deduplicates by content hash, so
// identical plants would recover as one artifact and a pile of duplicates, and
// every manifest entry would carry the same SHA-256.
TEST(SoakImage, EachPlantIsADifferentFile) {
	const auto path = tempSoakImage("distinct");
	ASSERT_TRUE(writeSoakImage(path, kTestImageBytes, kTestPlants).hasValue());
	const auto image = readFileBytes(path);
	const auto plan = soakPlan(kTestImageBytes, kTestPlants);
	const auto at = [&image](std::uint64_t offset) {
		return std::vector<std::byte>(
			image.begin() + static_cast<std::ptrdiff_t>(offset),
			image.begin() + static_cast<std::ptrdiff_t>(offset + kSoakPlantBytes));
	};
	EXPECT_NE(at(plan.at(0).offset), at(plan.at(1).offset));
	EXPECT_NE(at(plan.at(1).offset), at(plan.at(2).offset));
	removeSoakImage(path);
}

TEST(SoakImage, GenerationIsDeterministic) {
	const auto first = tempSoakImage("det-a");
	const auto second = tempSoakImage("det-b");
	ASSERT_TRUE(writeSoakImage(first, kTestImageBytes, kTestPlants).hasValue());
	ASSERT_TRUE(writeSoakImage(second, kTestImageBytes, kTestPlants).hasValue());
	EXPECT_EQ(readFileBytes(first), readFileBytes(second));
	removeSoakImage(first);
	removeSoakImage(second);
}

TEST(SoakImage, RecordsThePlanBesideTheImage) {
	const auto path = tempSoakImage("plan");
	ASSERT_TRUE(writeSoakImage(path, kTestImageBytes, kTestPlants).hasValue());
	const auto recorded = readFileText(soakPlanPath(path));
	EXPECT_EQ(recorded, "0 32768\n262144 32768\n524288 32768\n786432 32768\n");
	removeSoakImage(path);
}

TEST(SoakImage, RefusesToWriteAnImageWithNoRoomForItsPlants) {
	const auto path = tempSoakImage("refused");
	EXPECT_FALSE(writeSoakImage(path, kSoakPlantBytes, 2).hasValue());
	removeSoakImage(path);
}

} // namespace
