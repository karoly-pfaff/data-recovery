// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/RegionSet.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/fs/Types.hpp"

namespace {

using revenant::carve::ScanRegion;
using revenant::fs::Extent;
using revenant::recovery::RegionSet;

[[nodiscard]] Extent at(std::uint64_t offset, std::uint64_t length) {
	return Extent{.deviceOffset = offset, .lengthBytes = length};
}

[[nodiscard]] RegionSet claiming(const std::vector<Extent>& regions) {
	RegionSet set;
	for (const auto& region : regions) {
		set.add(region);
	}
	return set;
}

TEST(RegionSet, ClaimsNothingUntilSomethingIsAdded) {
	EXPECT_FALSE(RegionSet{}.overlaps(at(0, 1000)));
	EXPECT_EQ(RegionSet{}.claimedBytes(), 0U);
}

TEST(RegionSet, OverlapsARegionInsideAClaimedOne) {
	EXPECT_TRUE(claiming({at(100, 100)}).overlaps(at(150, 10)));
}

TEST(RegionSet, OverlapsOnTheFirstAndLastClaimedByte) {
	const auto set = claiming({at(100, 100)});
	EXPECT_TRUE(set.overlaps(at(90, 11)));
	EXPECT_TRUE(set.overlaps(at(199, 50)));
}

// Touching end-to-end is not overlapping: the byte at 200 belongs to nobody
// yet, and a file may legitimately begin exactly where another ended.
TEST(RegionSet, DoesNotOverlapARegionThatMerelyTouches) {
	const auto set = claiming({at(100, 100)});
	EXPECT_FALSE(set.overlaps(at(200, 50)));
	EXPECT_FALSE(set.overlaps(at(50, 50)));
}

TEST(RegionSet, DoesNotOverlapAnEmptyRegion) {
	EXPECT_FALSE(claiming({at(100, 100)}).overlaps(at(150, 0)));
}

TEST(RegionSet, FindsAnOverlapAgainstAnyOfManyClaims) {
	const auto set = claiming({at(1000, 10), at(10, 10), at(500, 10)});
	EXPECT_TRUE(set.overlaps(at(505, 1)));
	EXPECT_FALSE(set.overlaps(at(600, 300)));
}

TEST(RegionSet, FusesOverlappingAndTouchingClaims) {
	const auto set = claiming({at(100, 100), at(150, 100), at(250, 50)});
	EXPECT_EQ(set.claimedBytes(), 200U);
	EXPECT_TRUE(set.overlaps(at(299, 1)));
	EXPECT_FALSE(set.overlaps(at(300, 1)));
}

TEST(RegionSet, ClaimingInsideAnExistingRegionChangesNothing) {
	const auto set = claiming({at(100, 400), at(200, 100)});
	EXPECT_EQ(set.claimedBytes(), 400U);
}

TEST(RegionSet, GapsAreTheComplementClippedToTheDevice) {
	const auto set = claiming({at(100, 100), at(900, 500)});
	const std::vector<ScanRegion> expected{
		ScanRegion{.offset = 0, .lengthBytes = 100},
		ScanRegion{.offset = 200, .lengthBytes = 700}};
	EXPECT_EQ(set.gaps(1000), expected);
}

TEST(RegionSet, AnEmptySetLeavesTheWholeDeviceAsOneGap) {
	const std::vector<ScanRegion> expected{ScanRegion{.offset = 0, .lengthBytes = 1000}};
	EXPECT_EQ(RegionSet{}.gaps(1000), expected);
}

} // namespace
