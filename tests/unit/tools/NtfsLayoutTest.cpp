// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/NtfsLayout.hpp"

#include <gtest/gtest.h>

namespace {

using revenant::imagegen::ntfs::makeLayout;

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

} // namespace
