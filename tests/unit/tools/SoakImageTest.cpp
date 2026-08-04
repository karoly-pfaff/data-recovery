// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/SoakImage.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "imagegen/FixtureJpeg.hpp"
#include "imagegen/PatternWriter.hpp"
#include "revenant/core/Error.hpp"
#include "support/FixtureContent.hpp"

namespace {

using revenant::imagegen::fixtureJpeg;
using revenant::imagegen::kJpegFrameBytes;
using revenant::imagegen::kJpegHeaderBytes;
using revenant::imagegen::kSectorBytes;
using revenant::imagegen::kSoakPlantBytes;
using revenant::imagegen::Plant;
using revenant::imagegen::soakPlan;
using revenant::imagegen::soakPlanPath;
using revenant::imagegen::stampJpegPayload;
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

// Stands in for a plant whose bytes do not open with SOI. Not a valid offset:
// the image is a mebibyte, so this can never collide with a real one.
constexpr std::uint64_t kNoJpegHere = ~std::uint64_t{0};

std::filesystem::path tempSoakImage(const std::string& name) {
	return std::filesystem::temp_directory_path() / ("revenant-soak-" + name + ".img");
}

void removeSoakImage(const std::filesystem::path& path) {
	std::filesystem::remove(path);
	std::filesystem::remove(soakPlanPath(path));
}

// The bytes of one plant, so a test can compare two of them without spelling
// the iterator arithmetic twice.
[[nodiscard]] std::vector<std::byte>
plantAt(const std::vector<std::byte>& image, std::uint64_t offset) {
	const auto first = image.begin() + static_cast<std::ptrdiff_t>(offset);
	return {first, first + static_cast<std::ptrdiff_t>(kSoakPlantBytes)};
}

// Every planted offset, so the plan can be compared as a whole.
[[nodiscard]] std::vector<std::uint64_t> plannedOffsets(const std::vector<Plant>& plan) {
	std::vector<std::uint64_t> offsets;
	offsets.reserve(plan.size());
	for (const Plant& plant : plan) {
		offsets.push_back(plant.offset);
	}
	return offsets;
}

// The same list, but an offset is kept only where a JPEG really opens. Gathered
// rather than asserted plant by plant, so one comparison names every plant that
// is missing instead of one failure per plant — and so the assertion macros stay
// out of a loop, where they cost more cognitive complexity than the test has.
[[nodiscard]] std::vector<std::uint64_t>
jpegOffsetsIn(const std::vector<std::byte>& image, const std::vector<Plant>& plan) {
	std::vector<std::uint64_t> found;
	found.reserve(plan.size());
	for (const Plant& plant : plan) {
		const bool opensWithSoi =
			image.at(plant.offset) == kSoi0 && image.at(plant.offset + 1) == kSoi1;
		found.push_back(opensWithSoi ? plant.offset : kNoJpegHere);
	}
	return found;
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
	const auto plan = soakPlan(kTestImageBytes, kTestPlants);
	ASSERT_EQ(image.size(), kTestImageBytes);
	EXPECT_EQ(jpegOffsetsIn(image, plan), plannedOffsets(plan));
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
	EXPECT_NE(plantAt(image, plan.at(0).offset), plantAt(image, plan.at(1).offset));
	EXPECT_NE(plantAt(image, plan.at(1).offset), plantAt(image, plan.at(2).offset));
	removeSoakImage(path);
}

// The stamp's own guard, reached from here because the soak writer never can:
// it always hands over a 32 KiB JPEG. Without a caller that passes something
// shorter, the branch is dead code that only looks defensive.
TEST(SoakImage, StampingAJpegTooShortToHoldTheStampLeavesItAlone) {
	std::vector<std::byte> tiny(kJpegFrameBytes - 1, std::byte{0x5A});
	const auto before = tiny;
	stampJpegPayload(tiny, 0xDEADBEEF);
	EXPECT_EQ(tiny, before);
}

// Exactly the frame and no payload: the guard lets this through, and there is
// still nothing to stamp. The guard is not the whole boundary — it exists to
// stop `size - kJpegFrameBytes` wrapping below it, which is why the test above
// is the one that matters.
TEST(SoakImage, AJpegOfExactlyTheFrameHasNoPayloadToStamp) {
	std::vector<std::byte> jpeg(kJpegFrameBytes, std::byte{0x5A});
	const auto before = jpeg;
	stampJpegPayload(jpeg, 0xDEADBEEF);
	EXPECT_EQ(jpeg, before);
}

// One byte of payload is one byte of stamp: the first byte past the header
// changes and nothing else does.
TEST(SoakImage, AJpegWithOnePayloadByteGetsOneStampedByte) {
	std::vector<std::byte> jpeg(kJpegFrameBytes + 1, std::byte{0x5A});
	const auto before = jpeg;
	stampJpegPayload(jpeg, 0xDEADBEEF);
	EXPECT_NE(jpeg, before);
	EXPECT_EQ(jpeg.size(), before.size());
	EXPECT_EQ(jpeg.at(kJpegHeaderBytes - 1), std::byte{0x5A});
	EXPECT_NE(jpeg.at(kJpegHeaderBytes), std::byte{0x5A});
}

// And a real plant: stamped in the entropy run, so it is still a JPEG the
// carver validates — SOI at the front, EOI at the back, same length.
TEST(SoakImage, StampingChangesTheEntropyRunAndLeavesTheFrameAlone) {
	auto jpeg = fixtureJpeg(kSoakPlantBytes);
	const auto before = jpeg;
	stampJpegPayload(jpeg, 0xDEADBEEF);
	EXPECT_NE(jpeg, before);
	EXPECT_EQ(jpeg.size(), before.size());
	EXPECT_EQ(jpeg.at(0), kSoi0);
	EXPECT_EQ(jpeg.at(1), kSoi1);
	EXPECT_EQ(jpeg.back(), std::byte{0xD9});
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

// Spelled literally, because the soak's comparison script names this file from
// the outside and a rename would strand it.
TEST(SoakImage, ThePlanSitsBesideTheImageUnderADotPlanSuffix) {
	const auto path = tempSoakImage("suffix");
	EXPECT_EQ(soakPlanPath(path).string(), path.string() + ".plan");
}

TEST(SoakImage, RecordsThePlanBesideTheImage) {
	const auto path = tempSoakImage("plan");
	ASSERT_TRUE(writeSoakImage(path, kTestImageBytes, kTestPlants).hasValue());
	const auto recorded = readFileText(soakPlanPath(path));
	EXPECT_EQ(recorded, "0 32768\n262144 32768\n524288 32768\n786432 32768\n");
	removeSoakImage(path);
}

// The same contract the pattern writer keeps: a path that cannot be opened is
// an I/O failure with no offset, not one claiming the whole image was written.
TEST(SoakImage, AnUnopenableImageFailsWithNoOffset) {
	const auto path = std::filesystem::temp_directory_path() / "no-such-dir" / "soak.img";
	const auto written = writeSoakImage(path, kTestImageBytes, kTestPlants);
	ASSERT_FALSE(written.hasValue());
	EXPECT_EQ(written.error().code, revenant::ErrorCode::kIoFailure);
	EXPECT_EQ(written.error().offset, 0U);
}

TEST(SoakImage, RefusesToWriteAnImageWithNoRoomForItsPlants) {
	const auto path = tempSoakImage("refused");
	EXPECT_FALSE(writeSoakImage(path, kSoakPlantBytes, 2).hasValue());
	removeSoakImage(path);
}

} // namespace
