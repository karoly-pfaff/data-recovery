// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/Arbitration.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Candidate.hpp"

namespace {

using revenant::Confidence;
using revenant::fs::Extent;
using revenant::fs::Timestamps;
using revenant::recovery::arbitrate;
using revenant::recovery::Candidate;
using revenant::recovery::CandidateSource;

// One candidate over a single region, spelled the way each test needs it.
struct Claim {
	std::string_view name;
	std::uint64_t offset;
	std::uint64_t length;
	Confidence confidence;
	CandidateSource source;
};

[[nodiscard]] Candidate candidateOf(const Claim& claim) {
	return Candidate{
		.name = std::string{claim.name},
		.extents = {Extent{.deviceOffset = claim.offset, .lengthBytes = claim.length}},
		.residentContent = {},
		.timestamps = Timestamps{.created = 0, .modified = 0, .accessed = 0},
		.confidence = claim.confidence,
		.source = claim.source};
}

[[nodiscard]] std::vector<Candidate> claims(const std::vector<Claim>& list) {
	std::vector<Candidate> candidates;
	candidates.reserve(list.size());
	for (const auto& claim : list) {
		candidates.push_back(candidateOf(claim));
	}
	return candidates;
}

[[nodiscard]] std::vector<std::string> namesOf(const std::vector<Candidate>& candidates) {
	std::vector<std::string> names;
	names.reserve(candidates.size());
	for (const auto& candidate : candidates) {
		names.push_back(candidate.name);
	}
	return names;
}

constexpr Claim kNamedFile{
	.name = "photos/holiday.jpg",
	.offset = 1000,
	.length = 500,
	.confidence = Confidence::kValid,
	.source = CandidateSource::kFilesystem};

TEST(Arbitration, CandidatesThatDoNotCompeteAllWin) {
	const auto decided = arbitrate(claims(
		{kNamedFile,
		 Claim{
			 .name = "jpg",
			 .offset = 5000,
			 .length = 500,
			 .confidence = Confidence::kUncertain,
			 .source = CandidateSource::kCarve}}));
	EXPECT_EQ(decided.winners.size(), 2U);
	EXPECT_EQ(decided.suppressed, 0U);
}

// The ADR-0006 case: a weak carve of bytes a named entry already explains is
// indexed and then discarded, instead of cluttering the output beside it.
TEST(Arbitration, AWeakCarveLosesTheRegionToANamedEntry) {
	const auto decided = arbitrate(claims(
		{kNamedFile,
		 Claim{
			 .name = "swf",
			 .offset = 1100,
			 .length = 200,
			 .confidence = Confidence::kUncertain,
			 .source = CandidateSource::kCarve}}));
	EXPECT_EQ(namesOf(decided.winners), std::vector<std::string>{"photos/holiday.jpg"});
	EXPECT_EQ(decided.suppressed, 1U);
}

// Discovery order is an accident of how the device was scanned; the outcome
// must not depend on it.
TEST(Arbitration, TheOutcomeDoesNotDependOnDiscoveryOrder) {
	const auto weak = Claim{
		.name = "swf",
		.offset = 1100,
		.length = 200,
		.confidence = Confidence::kUncertain,
		.source = CandidateSource::kCarve};
	const auto first = arbitrate(claims({kNamedFile, weak}));
	const auto second = arbitrate(claims({weak, kNamedFile}));
	EXPECT_EQ(namesOf(first.winners), namesOf(second.winners));
}

// Accepting the non-overlapping part would emit exactly the fragment
// arbitration exists to remove.
TEST(Arbitration, ACandidateOverlappingOnlyPartlyStillLosesWhole) {
	const auto decided = arbitrate(claims(
		{kNamedFile,
		 Claim{
			 .name = "jpg",
			 .offset = 1400,
			 .length = 4000,
			 .confidence = Confidence::kUncertain,
			 .source = CandidateSource::kCarve}}));
	EXPECT_EQ(decided.winners.size(), 1U);
	EXPECT_EQ(decided.suppressed, 1U);
}

TEST(Arbitration, TheFilesystemWinsTheRegionAtEqualConfidence) {
	const auto decided = arbitrate(claims(
		{Claim{
			 .name = "jpg",
			 .offset = 1000,
			 .length = 500,
			 .confidence = Confidence::kValid,
			 .source = CandidateSource::kCarve},
		 kNamedFile}));
	EXPECT_EQ(namesOf(decided.winners), std::vector<std::string>{"photos/holiday.jpg"});
}

TEST(Arbitration, AtEqualConfidenceAndSourceTheLowerOffsetWins) {
	const auto decided = arbitrate(claims(
		{Claim{
			 .name = "later",
			 .offset = 1200,
			 .length = 500,
			 .confidence = Confidence::kUncertain,
			 .source = CandidateSource::kCarve},
		 Claim{
			 .name = "earlier",
			 .offset = 1000,
			 .length = 500,
			 .confidence = Confidence::kUncertain,
			 .source = CandidateSource::kCarve}}));
	EXPECT_EQ(namesOf(decided.winners), std::vector<std::string>{"earlier"});
}

// The two confidence scales measure different things. A carver grades the
// structure of the bytes in front of it; a filesystem entry knows the name,
// the timestamps, and which runs the content is actually spread across — so an
// uncertain named entry is still the better recovery of its own region.
TEST(Arbitration, AnUncertainNamedEntryBeatsAConfidentCarveOfTheSameBytes) {
	const auto decided = arbitrate(claims(
		{Claim{
			 .name = "orphan.jpg",
			 .offset = 1000,
			 .length = 500,
			 .confidence = Confidence::kUncertain,
			 .source = CandidateSource::kFilesystem},
		 Claim{
			 .name = "jpg",
			 .offset = 1000,
			 .length = 500,
			 .confidence = Confidence::kValid,
			 .source = CandidateSource::kCarve}}));
	EXPECT_EQ(namesOf(decided.winners), std::vector<std::string>{"orphan.jpg"});
}

TEST(Arbitration, ARejectedCandidateNeverWins) {
	const auto decided = arbitrate(claims({Claim{
		.name = "junk",
		.offset = 1000,
		.length = 500,
		.confidence = Confidence::kRejected,
		.source = CandidateSource::kCarve}}));
	EXPECT_TRUE(decided.winners.empty());
	EXPECT_EQ(decided.suppressed, 1U);
}

// Resident content occupies no device region it could lose.
TEST(Arbitration, ACandidateWithNoExtentsAlwaysWins) {
	std::vector<Candidate> candidates = claims({kNamedFile});
	candidates.push_back(
		Candidate{
			.name = "notes.txt",
			.extents = {},
			.residentContent = {std::byte{'h'}},
			.timestamps = Timestamps{.created = 0, .modified = 0, .accessed = 0},
			.confidence = Confidence::kValid,
			.source = CandidateSource::kFilesystem});
	const auto decided = arbitrate(std::move(candidates));
	EXPECT_EQ(decided.winners.size(), 2U);
	EXPECT_EQ(decided.suppressed, 0U);
}

TEST(Arbitration, TwoResidentCandidatesDoNotBlockEachOther) {
	const auto resident = Candidate{
		.name = "notes.txt",
		.extents = {},
		.residentContent = {std::byte{'h'}},
		.timestamps = Timestamps{.created = 0, .modified = 0, .accessed = 0},
		.confidence = Confidence::kValid,
		.source = CandidateSource::kFilesystem};
	const auto decided = arbitrate({resident, resident});
	EXPECT_EQ(decided.winners.size(), 2U);
}

TEST(Arbitration, ReportsWinnersInDeviceOrder) {
	const auto decided = arbitrate(claims(
		{Claim{
			 .name = "third",
			 .offset = 9000,
			 .length = 100,
			 .confidence = Confidence::kValid,
			 .source = CandidateSource::kCarve},
		 kNamedFile,
		 Claim{
			 .name = "second",
			 .offset = 5000,
			 .length = 100,
			 .confidence = Confidence::kUncertain,
			 .source = CandidateSource::kCarve}}));
	EXPECT_TRUE(std::ranges::is_sorted(decided.winners, {}, [](const Candidate& candidate) {
		return candidate.extents.front().deviceOffset;
	}));
	EXPECT_EQ(decided.winners.size(), 3U);
}

} // namespace
