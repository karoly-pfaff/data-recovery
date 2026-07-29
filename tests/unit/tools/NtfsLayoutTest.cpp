// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/NtfsLayout.hpp"

#include <gtest/gtest.h>

namespace {

using revenant::imagegen::ntfs::kMftRecordCount;
using revenant::imagegen::ntfs::makeLayout;
using revenant::imagegen::ntfs::makeLayoutForRecords;

TEST(NtfsLayout, ClusterSizeIsSectorsTimesSectorSize) {
	const auto layout = makeLayout();
	EXPECT_EQ(layout.bytesPerCluster(), layout.bytesPerSector * layout.sectorsPerCluster);
}

TEST(NtfsLayout, ByteOffsetsFollowTheClusterSize) {
	const auto layout = makeLayout();
	EXPECT_EQ(layout.clusterOffsetBytes(0), 0U);
	EXPECT_EQ(layout.clusterOffsetBytes(3), 3U * layout.bytesPerCluster());
	EXPECT_EQ(layout.mftOffsetBytes(), layout.clusterOffsetBytes(layout.mftStartCluster));
	EXPECT_EQ(layout.totalBytes(), layout.totalClusters * layout.bytesPerCluster());
}

TEST(NtfsLayout, MftIsSizedFromItsRecordCount) {
	const auto layout = makeLayout();
	const auto mftBytes = layout.mftClusterCount() * layout.bytesPerCluster();
	EXPECT_GE(mftBytes, layout.mftRecordCount * layout.mftRecordBytes);
	EXPECT_LT(mftBytes, (layout.mftRecordCount * layout.mftRecordBytes) + layout.bytesPerCluster());
}

TEST(NtfsLayout, DataAreaStartsBehindTheMft) {
	const auto layout = makeLayout();
	EXPECT_EQ(layout.dataStartCluster(), layout.mftStartCluster + layout.mftClusterCount());
	EXPECT_LT(layout.dataStartCluster(), layout.totalClusters);
}

TEST(NtfsLayout, TheMftFitsInsideTheVolume) {
	const auto layout = makeLayout();
	EXPECT_GT(layout.mftStartCluster, 0U);
	EXPECT_LE(layout.mftStartCluster + layout.mftClusterCount(), layout.totalClusters);
}

TEST(NtfsLayout, RecordSizeIsAPowerOfTwoTheBootSectorCanSpell) {
	const auto layout = makeLayout();
	EXPECT_EQ(layout.mftRecordBytes & (layout.mftRecordBytes - 1), 0U);
	EXPECT_GE(layout.mftRecordBytes, 256U);
	EXPECT_LE(layout.mftRecordBytes, 65536U);
}

// The scaled plan is the same plan at the fixture's own record count, so the
// volume every test asserts against does not move when a benchmark asks for a
// bigger one.
TEST(NtfsLayout, ScalingToTheFixturesOwnRecordCountChangesNothing) {
	EXPECT_EQ(makeLayoutForRecords(kMftRecordCount).totalBytes(), makeLayout().totalBytes());
	EXPECT_EQ(makeLayoutForRecords(kMftRecordCount).mftRecordCount, makeLayout().mftRecordCount);
}

TEST(NtfsLayout, MoreRecordsGrowTheMftAndTheVolumeWithIt) {
	const auto scaled = makeLayoutForRecords(kMftRecordCount * 8);
	EXPECT_GT(scaled.mftClusterCount(), makeLayout().mftClusterCount());
	EXPECT_GT(scaled.totalClusters, makeLayout().totalClusters);
}

// A bigger MFT must not eat the data region the fixture's files live in.
TEST(NtfsLayout, TheDataRegionKeepsItsSizeWhateverTheMftCosts) {
	const auto scaled = makeLayoutForRecords(kMftRecordCount * 8);
	const auto baseline = makeLayout();
	EXPECT_EQ(
		scaled.totalClusters - scaled.dataStartCluster(),
		baseline.totalClusters - baseline.dataStartCluster());
}

} // namespace
