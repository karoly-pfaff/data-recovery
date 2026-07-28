// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/ScanRegions.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "revenant/carve/SignatureScanner.hpp"

namespace {

using revenant::carve::ScanRegion;
using revenant::recovery::chunked;
using revenant::recovery::regionsFrom;

[[nodiscard]] std::vector<ScanRegion> twoGaps() {
	return {
		ScanRegion{.offset = 0, .lengthBytes = 100},
		ScanRegion{.offset = 500, .lengthBytes = 100}};
}

TEST(ScanRegions, ACursorOfZeroKeepsEverything) {
	EXPECT_EQ(regionsFrom(twoGaps(), 0), twoGaps());
}

TEST(ScanRegions, ACursorDropsWhatIsBehindIt) {
	const std::vector<ScanRegion> expected{ScanRegion{.offset = 500, .lengthBytes = 100}};
	EXPECT_EQ(regionsFrom(twoGaps(), 100), expected);
}

// The region the cursor lands inside is cut short, not dropped: the bytes past
// the cursor have not been searched yet.
TEST(ScanRegions, ACursorInsideARegionKeepsWhatIsLeftOfIt) {
	const std::vector<ScanRegion> expected{
		ScanRegion{.offset = 40, .lengthBytes = 60},
		ScanRegion{.offset = 500, .lengthBytes = 100}};
	EXPECT_EQ(regionsFrom(twoGaps(), 40), expected);
}

TEST(ScanRegions, ACursorPastEverythingLeavesNothingToScan) {
	EXPECT_TRUE(regionsFrom(twoGaps(), 600).empty());
}

TEST(ScanRegions, ARegionShorterThanTheChunkIsLeftAlone) {
	EXPECT_EQ(chunked(twoGaps(), 1000), twoGaps());
}

// This is what makes the checkpoint interval bounded on a device with a single
// enormous gap — carve-only over a formatted disk.
TEST(ScanRegions, ALongRegionIsCutIntoChunks) {
	const std::vector<ScanRegion> one{ScanRegion{.offset = 0, .lengthBytes = 250}};
	const std::vector<ScanRegion> expected{
		ScanRegion{.offset = 0, .lengthBytes = 100},
		ScanRegion{.offset = 100, .lengthBytes = 100},
		ScanRegion{.offset = 200, .lengthBytes = 50}};
	EXPECT_EQ(chunked(one, 100), expected);
}

TEST(ScanRegions, ChunkingCoversExactlyWhatItWasGiven) {
	const auto chunks = chunked(twoGaps(), 30);
	EXPECT_EQ(chunks.front().offset, 0U);
	EXPECT_EQ(chunks.back().offset + chunks.back().lengthBytes, 600U);
}

TEST(ScanRegions, NoRegionsChunkToNoChunks) {
	EXPECT_TRUE(chunked({}, 100).empty());
}

} // namespace
