// SPDX-License-Identifier: GPL-3.0-or-later
// story-0604: which of an artifact's bytes the device would not give up. The
// answer has to be exact in both directions — a byte marked that the device did
// supply makes the report noise, and a byte left unmarked that it did not is the
// silence this story exists to remove.
#include "recovery/Damage.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "revenant/core/io/BadRange.hpp"
#include "revenant/fs/Types.hpp"

namespace {

using revenant::BadRange;
using revenant::fs::Extent;
using revenant::recovery::inventedIn;

// A run that starts at the device's own zero — every case but the scoped one.
constexpr std::uint64_t kWholeDevice = 0;

TEST(Damage, AnUndamagedRunMarksNothing) {
	const std::vector<Extent> extents{Extent{.deviceOffset = 100, .lengthBytes = 100}};
	EXPECT_TRUE(inventedIn(extents, {}, kWholeDevice).empty());
}

TEST(Damage, AFaultInsideAnExtentIsMarkedExactly) {
	const std::vector<Extent> extents{Extent{.deviceOffset = 100, .lengthBytes = 100}};
	const std::vector<BadRange> damage{BadRange{.offsetBytes = 120, .lengthBytes = 30}};
	EXPECT_EQ(inventedIn(extents, damage, kWholeDevice), damage);
}

// The overlap, not the fault: a bad range wider than the file says nothing about
// bytes the file never claimed.
TEST(Damage, AFaultWiderThanTheExtentIsClippedToIt) {
	const std::vector<Extent> extents{Extent{.deviceOffset = 100, .lengthBytes = 100}};
	const std::vector<BadRange> damage{BadRange{.offsetBytes = 0, .lengthBytes = 1000}};
	const std::vector<BadRange> expected{BadRange{.offsetBytes = 100, .lengthBytes = 100}};
	EXPECT_EQ(inventedIn(extents, damage, kWholeDevice), expected);
}

// The two boundary cases that decide whether the arithmetic is half-open: a
// fault ending on the extent's first byte overlaps by one, and one byte earlier
// overlaps by none.
TEST(Damage, AFaultAbuttingTheExtentsFirstByteMarksThatByte) {
	const std::vector<Extent> extents{Extent{.deviceOffset = 100, .lengthBytes = 100}};
	const std::vector<BadRange> damage{BadRange{.offsetBytes = 50, .lengthBytes = 51}};
	const std::vector<BadRange> expected{BadRange{.offsetBytes = 100, .lengthBytes = 1}};
	EXPECT_EQ(inventedIn(extents, damage, kWholeDevice), expected);
}

TEST(Damage, AFaultEndingOneByteShortOfTheExtentMarksNothing) {
	const std::vector<Extent> extents{Extent{.deviceOffset = 100, .lengthBytes = 100}};
	const std::vector<BadRange> damage{BadRange{.offsetBytes = 50, .lengthBytes = 50}};
	EXPECT_TRUE(inventedIn(extents, damage, kWholeDevice).empty());
}

TEST(Damage, AFaultStartingOnTheExtentsLastByteMarksThatByte) {
	const std::vector<Extent> extents{Extent{.deviceOffset = 100, .lengthBytes = 100}};
	const std::vector<BadRange> damage{BadRange{.offsetBytes = 199, .lengthBytes = 50}};
	const std::vector<BadRange> expected{BadRange{.offsetBytes = 199, .lengthBytes = 1}};
	EXPECT_EQ(inventedIn(extents, damage, kWholeDevice), expected);
}

TEST(Damage, AFaultStartingJustPastTheExtentMarksNothing) {
	const std::vector<Extent> extents{Extent{.deviceOffset = 100, .lengthBytes = 100}};
	const std::vector<BadRange> damage{BadRange{.offsetBytes = 200, .lengthBytes = 50}};
	EXPECT_TRUE(inventedIn(extents, damage, kWholeDevice).empty());
}

// A fragmented file does not own the space between its fragments, and damage
// there is somebody else's problem.
TEST(Damage, AFaultInTheGapOfAFragmentedFileMarksNothing) {
	const std::vector<Extent> extents{
		Extent{.deviceOffset = 100, .lengthBytes = 50},
		Extent{.deviceOffset = 300, .lengthBytes = 50}};
	const std::vector<BadRange> damage{BadRange{.offsetBytes = 200, .lengthBytes = 50}};
	EXPECT_TRUE(inventedIn(extents, damage, kWholeDevice).empty());
}

TEST(Damage, EveryFragmentOfAFileIsChecked) {
	const std::vector<Extent> extents{
		Extent{.deviceOffset = 100, .lengthBytes = 50},
		Extent{.deviceOffset = 300, .lengthBytes = 50}};
	const std::vector<BadRange> damage{
		BadRange{.offsetBytes = 110, .lengthBytes = 10},
		BadRange{.offsetBytes = 310, .lengthBytes = 10}};
	EXPECT_EQ(inventedIn(extents, damage, kWholeDevice), damage);
}

// Resident content never touches the device through extents, so it can never be
// degraded — it was read out of a record that was itself read successfully.
TEST(Damage, AnArtifactWithNoExtentsIsNeverMarked) {
	const std::vector<BadRange> damage{BadRange{.offsetBytes = 0, .lengthBytes = 1000}};
	EXPECT_TRUE(inventedIn({}, damage, kWholeDevice).empty());
}

// A scoped run's extents are relative to its window; the map is not. The answer
// is stated in the device's coordinates, which are the numbers an operator can
// check against any other tool.
TEST(Damage, AScopedRunsExtentsAreTranslatedBeforeTheyAreCompared) {
	constexpr std::uint64_t kWindowStart = 1024;
	const std::vector<Extent> extents{Extent{.deviceOffset = 100, .lengthBytes = 100}};
	const std::vector<BadRange> damage{
		BadRange{.offsetBytes = kWindowStart + 120, .lengthBytes = 30}};
	EXPECT_EQ(inventedIn(extents, damage, kWindowStart), damage);
}

// The same fault, at the offset it would have had if nobody translated: it is
// outside the window's extent and must not be marked.
TEST(Damage, AScopedRunDoesNotMarkTheUntranslatedOffset) {
	constexpr std::uint64_t kWindowStart = 1024;
	const std::vector<Extent> extents{Extent{.deviceOffset = 100, .lengthBytes = 100}};
	const std::vector<BadRange> damage{BadRange{.offsetBytes = 120, .lengthBytes = 30}};
	EXPECT_TRUE(inventedIn(extents, damage, kWindowStart).empty());
}

} // namespace
