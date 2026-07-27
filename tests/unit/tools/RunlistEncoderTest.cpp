// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/RunlistEncoder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "revenant/fs/ntfs/Runlist.hpp"

namespace {

using revenant::fs::ntfs::DataRun;
using revenant::fs::ntfs::decodeRunlist;
using revenant::imagegen::ntfs::encodeRunlist;

// The encoder's contract is stated against the production decoder: whatever it
// emits must come back as the runs that went in.
[[nodiscard]] std::vector<DataRun> roundTrip(const std::vector<DataRun>& runs) {
	const auto encoded = encodeRunlist(runs);
	const auto decoded = decodeRunlist(encoded);
	EXPECT_TRUE(decoded.hasValue());
	return decoded.hasValue() ? decoded.value().runs : std::vector<DataRun>{};
}

TEST(RunlistEncoder, EncodesASingleRun) {
	const std::vector<DataRun> runs{
		DataRun{.startCluster = 0x5634, .lengthClusters = 0x18, .sparse = false}};
	const auto decoded = roundTrip(runs);
	ASSERT_EQ(decoded.size(), 1U);
	EXPECT_EQ(decoded.front().startCluster, 0x5634U);
	EXPECT_EQ(decoded.front().lengthClusters, 0x18U);
}

TEST(RunlistEncoder, EmitsTheCanonicalEncodingForTheTextbookRun) {
	const std::vector<DataRun> runs{
		DataRun{.startCluster = 0x5634, .lengthClusters = 0x18, .sparse = false}};
	const auto encoded = encodeRunlist(runs);
	const std::vector<std::byte> expected{
		std::byte{0x21},
		std::byte{0x18},
		std::byte{0x34},
		std::byte{0x56},
		std::byte{0x00}};
	EXPECT_EQ(encoded, expected);
}

TEST(RunlistEncoder, EncodesFragmentedRunsAsDeltas) {
	const std::vector<DataRun> runs{
		DataRun{.startCluster = 100, .lengthClusters = 4, .sparse = false},
		DataRun{.startCluster = 200, .lengthClusters = 8, .sparse = false}};
	const auto decoded = roundTrip(runs);
	ASSERT_EQ(decoded.size(), 2U);
	EXPECT_EQ(decoded.at(0).startCluster, 100U);
	EXPECT_EQ(decoded.at(1).startCluster, 200U);
	EXPECT_EQ(decoded.at(1).lengthClusters, 8U);
}

TEST(RunlistEncoder, EncodesABackwardsRunAsANegativeDelta) {
	const std::vector<DataRun> runs{
		DataRun{.startCluster = 500, .lengthClusters = 2, .sparse = false},
		DataRun{.startCluster = 300, .lengthClusters = 2, .sparse = false}};
	const auto decoded = roundTrip(runs);
	ASSERT_EQ(decoded.size(), 2U);
	EXPECT_EQ(decoded.at(0).startCluster, 500U);
	EXPECT_EQ(decoded.at(1).startCluster, 300U);
}

TEST(RunlistEncoder, EncodesASparseRunWithNoOffsetField) {
	const std::vector<DataRun> runs{
		DataRun{.startCluster = 40, .lengthClusters = 2, .sparse = false},
		DataRun{.startCluster = 0, .lengthClusters = 3, .sparse = true},
		DataRun{.startCluster = 50, .lengthClusters = 1, .sparse = false}};
	const auto decoded = roundTrip(runs);
	ASSERT_EQ(decoded.size(), 3U);
	EXPECT_TRUE(decoded.at(1).sparse);
	EXPECT_EQ(decoded.at(1).lengthClusters, 3U);
	// The hole holds no LCN, so the third run's delta is measured from the first.
	EXPECT_EQ(decoded.at(2).startCluster, 50U);
}

TEST(RunlistEncoder, ChoosesMinimalFieldWidths) {
	const std::vector<DataRun> runs{
		DataRun{.startCluster = 1, .lengthClusters = 1, .sparse = false}};
	// header + one length byte + one offset byte + terminator
	EXPECT_EQ(encodeRunlist(runs).size(), 4U);
}

TEST(RunlistEncoder, WidensFieldsForLargeValues) {
	const std::vector<DataRun> runs{
		DataRun{.startCluster = 0x0102030405, .lengthClusters = 0x010203, .sparse = false}};
	const auto decoded = roundTrip(runs);
	ASSERT_EQ(decoded.size(), 1U);
	EXPECT_EQ(decoded.front().startCluster, 0x0102030405U);
	EXPECT_EQ(decoded.front().lengthClusters, 0x010203U);
}

TEST(RunlistEncoder, EncodesAnEmptyRunSetAsTheBareTerminator) {
	const std::vector<DataRun> runs;
	const std::vector<std::byte> expected{std::byte{0x00}};
	EXPECT_EQ(encodeRunlist(runs), expected);
}

} // namespace
