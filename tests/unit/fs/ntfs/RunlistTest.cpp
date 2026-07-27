// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/Runlist.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>
#include <vector>

#include "revenant/core/Error.hpp"

namespace {

using revenant::ErrorCode;
using revenant::fs::ntfs::decodeRunlist;
using revenant::fs::ntfs::kMaxDataRuns;

[[nodiscard]] std::vector<std::byte> runBytes(std::initializer_list<int> values) {
	std::vector<std::byte> out;
	out.reserve(values.size());
	for (const int value : values) {
		out.push_back(static_cast<std::byte>(value));
	}
	return out;
}

// `count` minimal sparse runs (header 0x01 = 1-byte length, no offset field),
// each one cluster long, followed by the end marker.
[[nodiscard]] std::vector<std::byte> sparseRuns(std::size_t count) {
	std::vector<std::byte> out;
	out.reserve((count * 2) + 1);
	for (std::size_t i = 0; i < count; ++i) {
		out.push_back(std::byte{0x01});
		out.push_back(std::byte{0x01});
	}
	out.push_back(std::byte{0x00});
	return out;
}

TEST(Runlist, DecodesCanonicalSingleRun) {
	const auto bytes = runBytes({0x21, 0x18, 0x34, 0x56, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_TRUE(result.hasValue());
	const auto& runlist = result.value();
	ASSERT_EQ(runlist.runs.size(), 1U);
	EXPECT_EQ(runlist.runs.front().startCluster, 0x5634U);
	EXPECT_EQ(runlist.runs.front().lengthClusters, 0x18U);
	EXPECT_FALSE(runlist.runs.front().sparse);
	EXPECT_EQ(runlist.totalClusters, 0x18U);
}

TEST(Runlist, DecodesEmptyRunlistAsNoRuns) {
	const auto bytes = runBytes({0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().runs.empty());
	EXPECT_EQ(result.value().totalClusters, 0U);
}

TEST(Runlist, AppliesPositiveOffsetDeltasAcrossRuns) {
	const auto bytes = runBytes({0x11, 0x04, 0x10, 0x11, 0x02, 0x08, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_TRUE(result.hasValue());
	const auto& runs = result.value().runs;
	ASSERT_EQ(runs.size(), 2U);
	EXPECT_EQ(runs.at(0).startCluster, 0x10U);
	EXPECT_EQ(runs.at(0).lengthClusters, 4U);
	EXPECT_EQ(runs.at(1).startCluster, 0x18U);
	EXPECT_EQ(runs.at(1).lengthClusters, 2U);
	EXPECT_EQ(result.value().totalClusters, 6U);
}

TEST(Runlist, AppliesNegativeOffsetDelta) {
	const auto bytes = runBytes({0x11, 0x04, 0x64, 0x11, 0x02, 0xF8, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_TRUE(result.hasValue());
	const auto& runs = result.value().runs;
	ASSERT_EQ(runs.size(), 2U);
	EXPECT_EQ(runs.at(0).startCluster, 100U);
	EXPECT_EQ(runs.at(1).startCluster, 92U);
}

TEST(Runlist, MarksZeroWidthOffsetRunAsSparse) {
	const auto bytes = runBytes({0x11, 0x04, 0x20, 0x01, 0x05, 0x11, 0x02, 0x10, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_TRUE(result.hasValue());
	const auto& runs = result.value().runs;
	ASSERT_EQ(runs.size(), 3U);
	EXPECT_FALSE(runs.at(0).sparse);
	EXPECT_TRUE(runs.at(1).sparse);
	EXPECT_EQ(runs.at(1).lengthClusters, 5U);
	EXPECT_FALSE(runs.at(2).sparse);
	// The sparse run consumes no LCN: the delta applies to the run before it.
	EXPECT_EQ(runs.at(2).startCluster, 0x30U);
	EXPECT_EQ(result.value().totalClusters, 11U);
}

TEST(Runlist, DecodesWideLengthAndOffsetFields) {
	const auto bytes = runBytes({0x48, 0x02, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x10, 0x00, 0x00, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_TRUE(result.hasValue());
	ASSERT_EQ(result.value().runs.size(), 1U);
	EXPECT_EQ(result.value().runs.front().startCluster, 0x1000U);
	EXPECT_EQ(result.value().runs.front().lengthClusters, 2U);
}

TEST(Runlist, RejectsEmptyInput) {
	const std::vector<std::byte> bytes;
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOutOfRange);
}

TEST(Runlist, RejectsMissingEndMarker) {
	const auto bytes = runBytes({0x11, 0x04, 0x20});
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOutOfRange);
}

TEST(Runlist, RejectsZeroWidthLengthField) {
	const auto bytes = runBytes({0x10, 0x20, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
	EXPECT_EQ(result.error().offset, 0U);
}

TEST(Runlist, RejectsLengthFieldWiderThanEightBytes) {
	const auto bytes = runBytes({0x09, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(Runlist, RejectsOffsetFieldWiderThanEightBytes) {
	const auto bytes = runBytes({0x91, 0x01, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(Runlist, RejectsRunFieldsTruncatedBySpanEnd) {
	const auto bytes = runBytes({0x21, 0x18, 0x34});
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOutOfRange);
}

TEST(Runlist, RejectsZeroClusterRun) {
	const auto bytes = runBytes({0x11, 0x00, 0x10, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(Runlist, RejectsDeltaDrivingTheStartClusterNegative) {
	const auto bytes = runBytes({0x11, 0x04, 0xF0, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// Two maximal positive deltas in a row: the first lands the LCN on INT64_MAX,
// the second would carry it past the signed range the delta arithmetic lives in.
TEST(Runlist, RejectsDeltaOverflowingTheStartCluster) {
	const auto bytes = runBytes({0x81, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x81,
								 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOverflow);
}

TEST(Runlist, RejectsClusterTotalOverflow) {
	const auto bytes = runBytes({0x18, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x18,
								 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x00});
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOverflow);
}

TEST(Runlist, AcceptsRunCountAtTheLimit) {
	const auto bytes = sparseRuns(kMaxDataRuns);
	const auto result = decodeRunlist(bytes);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().runs.size(), kMaxDataRuns);
}

TEST(Runlist, RejectsRunCountAboveTheLimit) {
	const auto bytes = sparseRuns(kMaxDataRuns + 1);
	const auto result = decodeRunlist(bytes);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOutOfRange);
}

} // namespace
