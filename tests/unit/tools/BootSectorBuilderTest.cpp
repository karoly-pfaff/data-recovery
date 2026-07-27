// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/BootSectorBuilder.hpp"

#include <gtest/gtest.h>

#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"

namespace {

using revenant::fs::ntfs::parseBootSector;
using revenant::imagegen::ntfs::buildBootSector;
using revenant::imagegen::ntfs::makeLayout;

TEST(BootSectorBuilder, ProducesOneSector) {
	EXPECT_EQ(buildBootSector(makeLayout()).size(), makeLayout().bytesPerSector);
}

// The builder is specified against the production parser: what it writes is
// exactly what parseBootSector reads back.
TEST(BootSectorBuilder, ParsesBackAsTheLayoutGeometry) {
	const auto layout = makeLayout();
	const auto sector = buildBootSector(layout);
	const auto geometry = parseBootSector(sector);
	ASSERT_TRUE(geometry.hasValue());
	EXPECT_EQ(geometry.value().bytesPerSector, layout.bytesPerSector);
	EXPECT_EQ(geometry.value().bytesPerCluster, layout.bytesPerCluster());
	EXPECT_EQ(geometry.value().totalClusters, layout.totalClusters);
	EXPECT_EQ(geometry.value().mftOffsetBytes, layout.mftOffsetBytes());
	EXPECT_EQ(geometry.value().bytesPerMftRecord, layout.mftRecordBytes);
}

TEST(BootSectorBuilder, IsDeterministic) {
	EXPECT_EQ(buildBootSector(makeLayout()), buildBootSector(makeLayout()));
}

} // namespace
