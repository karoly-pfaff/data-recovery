// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/OutputName.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "revenant/core/Confidence.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Candidate.hpp"

namespace {

using revenant::Confidence;
using revenant::fs::Timestamps;
using revenant::recovery::Candidate;
using revenant::recovery::CandidateSource;
using revenant::recovery::outputNameFor;

[[nodiscard]] Candidate named(std::string_view name, CandidateSource source) {
	return Candidate{
		.name = std::string{name},
		.extents = {},
		.residentContent = {},
		.timestamps = Timestamps{.created = 0, .modified = 0, .accessed = 0},
		.confidence = Confidence::kValid,
		.source = source};
}

TEST(OutputName, ANamedEntryKeepsThePathItHadInTheVolume) {
	const auto entry = named("photos/holiday.jpg", CandidateSource::kFilesystem);
	EXPECT_EQ(outputNameFor(entry, 7), "photos/holiday.jpg");
}

TEST(OutputName, ACarvedFileGoesIntoATypeBucketUnderItsOrdinal) {
	const auto carved = named("jpg", CandidateSource::kCarve);
	EXPECT_EQ(outputNameFor(carved, 1), "carved/jpg/f00000001.jpg");
}

TEST(OutputName, TheOrdinalIsZeroPaddedAndFollowsDeviceOrder) {
	const auto carved = named("png", CandidateSource::kCarve);
	EXPECT_EQ(outputNameFor(carved, 0), "carved/png/f00000000.png");
	EXPECT_EQ(outputNameFor(carved, 42), "carved/png/f00000042.png");
}

TEST(OutputName, ACarvedFileWithNoFormatLandsInTheUnknownBucket) {
	const auto carved = named("", CandidateSource::kCarve);
	EXPECT_EQ(outputNameFor(carved, 3), "carved/bin/f00000003.bin");
}

// A carver's extension is data. It may name a bucket only if it looks like one
// of ours; anything else is refused rather than allowed to steer the path.
TEST(OutputName, AnExtensionThatCouldSteerThePathIsRefused) {
	EXPECT_EQ(
		outputNameFor(named("../../etc", CandidateSource::kCarve), 1),
		"carved/bin/f00000001.bin");
	EXPECT_EQ(outputNameFor(named("a/b", CandidateSource::kCarve), 1), "carved/bin/f00000001.bin");
	EXPECT_EQ(outputNameFor(named("JPG", CandidateSource::kCarve), 1), "carved/bin/f00000001.bin");
	EXPECT_EQ(
		outputNameFor(named("waytoolongextension", CandidateSource::kCarve), 1),
		"carved/bin/f00000001.bin");
}

} // namespace
