// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "revenant/core/Error.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace {

using revenant::ErrorCode;
using revenant::fs::ntfs::DataRun;
using revenant::fs::ntfs::NtfsGeometry;
using revenant::fs::ntfs::Runlist;
using revenant::fs::ntfs::runlistExtents;

// 64-bit so the byte arithmetic in the expectations below never widens a
// 32-bit multiplication after the fact.
constexpr std::uint64_t kBytesPerCluster = 4096;
constexpr std::uint64_t kVolumeClusters = 1000;

[[nodiscard]] NtfsGeometry testGeometry() {
	return NtfsGeometry{
		.bytesPerSector = 512,
		.bytesPerCluster = static_cast<std::uint32_t>(kBytesPerCluster),
		.totalClusters = kVolumeClusters,
		.mftOffsetBytes = 0,
		.bytesPerMftRecord = 1024};
}

// Two allocated runs, five clusters of payload in total.
[[nodiscard]] Runlist twoRunList() {
	return Runlist{
		.runs =
			{DataRun{.startCluster = 10, .lengthClusters = 2, .sparse = false},
			 DataRun{.startCluster = 20, .lengthClusters = 3, .sparse = false}},
		.totalClusters = 5};
}

TEST(RunlistExtents, MapsRunsToDeviceByteExtents) {
	const auto result = runlistExtents(twoRunList(), testGeometry(), 5 * kBytesPerCluster);
	ASSERT_TRUE(result.hasValue());
	const auto& extents = result.value();
	ASSERT_EQ(extents.size(), 2U);
	EXPECT_EQ(extents.at(0).deviceOffset, 10 * kBytesPerCluster);
	EXPECT_EQ(extents.at(0).lengthBytes, 2 * kBytesPerCluster);
	EXPECT_EQ(extents.at(1).deviceOffset, 20 * kBytesPerCluster);
	EXPECT_EQ(extents.at(1).lengthBytes, 3 * kBytesPerCluster);
}

TEST(RunlistExtents, TrimsTheFinalExtentToRealSize) {
	constexpr std::uint64_t kRealSize = (4 * kBytesPerCluster) + 100;
	const auto result = runlistExtents(twoRunList(), testGeometry(), kRealSize);
	ASSERT_TRUE(result.hasValue());
	const auto& extents = result.value();
	ASSERT_EQ(extents.size(), 2U);
	EXPECT_EQ(extents.at(0).lengthBytes, 2 * kBytesPerCluster);
	EXPECT_EQ(extents.at(1).lengthBytes, kRealSize - (2 * kBytesPerCluster));
}

TEST(RunlistExtents, DropsRunsBeyondRealSize) {
	const auto result = runlistExtents(twoRunList(), testGeometry(), kBytesPerCluster);
	ASSERT_TRUE(result.hasValue());
	ASSERT_EQ(result.value().size(), 1U);
	EXPECT_EQ(result.value().front().lengthBytes, kBytesPerCluster);
}

TEST(RunlistExtents, MapsEmptyRunlistToNoExtents) {
	const auto result = runlistExtents(Runlist{}, testGeometry(), 0);
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().empty());
}

TEST(RunlistExtents, RejectsRunEndingPastTheVolume) {
	const Runlist runlist{
		.runs =
			{DataRun{.startCluster = kVolumeClusters - 1, .lengthClusters = 2, .sparse = false}},
		.totalClusters = 2};
	const auto result = runlistExtents(runlist, testGeometry(), kBytesPerCluster);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(RunlistExtents, RejectsRealSizeAboveTheAllocation) {
	const auto result = runlistExtents(twoRunList(), testGeometry(), (5 * kBytesPerCluster) + 1);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(RunlistExtents, RejectsSparseRun) {
	const Runlist runlist{
		.runs = {DataRun{.startCluster = 0, .lengthClusters = 2, .sparse = true}},
		.totalClusters = 2};
	const auto result = runlistExtents(runlist, testGeometry(), kBytesPerCluster);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(RunlistExtents, RejectsGeometryWhoseVolumeSizeOverflows) {
	auto geometry = testGeometry();
	geometry.totalClusters = std::numeric_limits<std::uint64_t>::max();
	const auto result = runlistExtents(twoRunList(), geometry, 5 * kBytesPerCluster);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOverflow);
}

} // namespace
