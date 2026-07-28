// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/ByteAccounting.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <ostream>
#include <utility>
#include <vector>

#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::carve {

// gtest prints a byte dump without this; a gap list is far easier to read as
// offsets, and every failure here is about offsets.
static std::ostream& operator<<(std::ostream& out, const ScanRegion& region) {
	return out << "[" << region.offset << ", " << region.offset + region.lengthBytes << ")";
}

} // namespace revenant::carve

namespace {

using revenant::Confidence;
using revenant::carve::ScanRegion;
using revenant::fs::EntryState;
using revenant::fs::Extent;
using revenant::fs::RecoveredEntry;
using revenant::fs::Timestamps;
using revenant::recovery::ByteAccounting;
using revenant::recovery::kMaxAccountedRegions;

constexpr std::uint64_t kDeviceBytes = 1000;

[[nodiscard]] RecoveredEntry entryWith(std::vector<Extent> extents, Confidence grade) {
	return RecoveredEntry{
		.path = "photos/holiday.jpg",
		.sizeInBytes = 0,
		.extents = std::move(extents),
		.residentContent = {},
		.timestamps = Timestamps{},
		.state = EntryState::kDeleted,
		.recoverability = grade};
}

[[nodiscard]] ByteAccounting accounting(std::vector<Extent> extents) {
	ByteAccounting accounted;
	accounted.account(entryWith(std::move(extents), Confidence::kValid));
	return accounted;
}

TEST(ByteAccounting, NothingAccountedLeavesTheWholeDeviceToScan) {
	const std::vector<ScanRegion> expected{ScanRegion{.offset = 0, .lengthBytes = kDeviceBytes}};
	EXPECT_EQ(ByteAccounting{}.gaps(kDeviceBytes), expected);
}

TEST(ByteAccounting, ARegionInTheMiddleLeavesAGapOnEitherSide) {
	const auto accounted = accounting({Extent{.deviceOffset = 100, .lengthBytes = 50}});
	const std::vector<ScanRegion> expected{
		ScanRegion{.offset = 0, .lengthBytes = 100},
		ScanRegion{.offset = 150, .lengthBytes = 850}};
	EXPECT_EQ(accounted.gaps(kDeviceBytes), expected);
}

TEST(ByteAccounting, ARegionAtTheStartLeavesNoLeadingGap) {
	const auto accounted = accounting({Extent{.deviceOffset = 0, .lengthBytes = 100}});
	const std::vector<ScanRegion> expected{ScanRegion{.offset = 100, .lengthBytes = 900}};
	EXPECT_EQ(accounted.gaps(kDeviceBytes), expected);
}

TEST(ByteAccounting, ARegionReachingTheDeviceEndLeavesNoTrailingGap) {
	const auto accounted = accounting({Extent{.deviceOffset = 900, .lengthBytes = 100}});
	const std::vector<ScanRegion> expected{ScanRegion{.offset = 0, .lengthBytes = 900}};
	EXPECT_EQ(accounted.gaps(kDeviceBytes), expected);
}

TEST(ByteAccounting, ARegionCoveringEverythingLeavesNothingToScan) {
	const auto accounted = accounting({Extent{.deviceOffset = 0, .lengthBytes = kDeviceBytes}});
	EXPECT_TRUE(accounted.gaps(kDeviceBytes).empty());
}

// The set has to stay proportional to distinct regions, not to file count:
// a million contiguous files must not become a million gaps to walk.
TEST(ByteAccounting, OverlappingRegionsFuseIntoOne) {
	const auto accounted = accounting(
		{Extent{.deviceOffset = 100, .lengthBytes = 100},
		 Extent{.deviceOffset = 150, .lengthBytes = 100}});
	const std::vector<ScanRegion> expected{
		ScanRegion{.offset = 0, .lengthBytes = 100},
		ScanRegion{.offset = 250, .lengthBytes = 750}};
	EXPECT_EQ(accounted.gaps(kDeviceBytes), expected);
}

TEST(ByteAccounting, TouchingRegionsFuseIntoOne) {
	const auto accounted = accounting(
		{Extent{.deviceOffset = 150, .lengthBytes = 50},
		 Extent{.deviceOffset = 100, .lengthBytes = 50}});
	const std::vector<ScanRegion> expected{
		ScanRegion{.offset = 0, .lengthBytes = 100},
		ScanRegion{.offset = 200, .lengthBytes = 800}};
	EXPECT_EQ(accounted.gaps(kDeviceBytes), expected);
}

TEST(ByteAccounting, ARegionInsideAnotherAddsNothing) {
	const auto accounted = accounting(
		{Extent{.deviceOffset = 100, .lengthBytes = 400},
		 Extent{.deviceOffset = 200, .lengthBytes = 100}});
	const std::vector<ScanRegion> expected{
		ScanRegion{.offset = 0, .lengthBytes = 100},
		ScanRegion{.offset = 500, .lengthBytes = 500}};
	EXPECT_EQ(accounted.gaps(kDeviceBytes), expected);
}

// Hybrid orchestration calls an uncertain region a safety net: the carve pass
// looks there precisely because the metadata could not vouch for it.
TEST(ByteAccounting, AnUncertainEntryDoesNotSuppressScanning) {
	ByteAccounting accounted;
	accounted.account(
		entryWith({Extent{.deviceOffset = 100, .lengthBytes = 50}}, Confidence::kUncertain));
	const std::vector<ScanRegion> expected{ScanRegion{.offset = 0, .lengthBytes = kDeviceBytes}};
	EXPECT_EQ(accounted.gaps(kDeviceBytes), expected);
}

TEST(ByteAccounting, ARegionPastTheDeviceEndMovesNoBoundary) {
	const auto accounted = accounting({Extent{.deviceOffset = 2000, .lengthBytes = 100}});
	const std::vector<ScanRegion> expected{ScanRegion{.offset = 0, .lengthBytes = kDeviceBytes}};
	EXPECT_EQ(accounted.gaps(kDeviceBytes), expected);
}

TEST(ByteAccounting, ARegionStraddlingTheDeviceEndIsClipped) {
	const auto accounted = accounting({Extent{.deviceOffset = 900, .lengthBytes = 500}});
	const std::vector<ScanRegion> expected{ScanRegion{.offset = 0, .lengthBytes = 900}};
	EXPECT_EQ(accounted.gaps(kDeviceBytes), expected);
}

TEST(ByteAccounting, ReportsTheBytesItAccountsForOnceEachRegionIsFused) {
	const auto accounted = accounting(
		{Extent{.deviceOffset = 100, .lengthBytes = 100},
		 Extent{.deviceOffset = 150, .lengthBytes = 100},
		 Extent{.deviceOffset = 400, .lengthBytes = 50}});
	EXPECT_EQ(accounted.accountedBytes(), 200U);
	EXPECT_EQ(accounted.droppedRegions(), 0U);
}

// ADR-0009: the extent count comes off a disk, so the set is capped. Dropping
// is safe here in a way dropping a candidate would not be — less accounting
// only ever means more scanning.
TEST(ByteAccounting, DropsAndCountsExtentsPastTheCap) {
	std::vector<Extent> extents;
	extents.reserve(kMaxAccountedRegions + 2);
	for (std::uint64_t i = 0; i < kMaxAccountedRegions + 2; ++i) {
		extents.push_back(Extent{.deviceOffset = i * 4, .lengthBytes = 2});
	}
	const auto accounted = accounting(std::move(extents));
	EXPECT_EQ(accounted.droppedRegions(), 2U);
}

} // namespace
