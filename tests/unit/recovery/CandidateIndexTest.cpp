// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/CandidateIndex.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "support/TempDir.hpp"

namespace {

using revenant::Confidence;
using revenant::ErrorCode;
using revenant::fs::Extent;
using revenant::fs::Timestamps;
using revenant::recovery::Candidate;
using revenant::recovery::CandidateIndex;
using revenant::recovery::CandidateSource;
using revenant::recovery::kMaxCandidateNameBytes;
using revenant::recovery::readIndex;
using revenant::testing::TempDir;

constexpr std::string_view kRecordFile = "candidates.idx";

[[nodiscard]] Candidate namedEntry() {
	return Candidate{
		.name = "photos/holiday.jpg",
		.extents =
			{Extent{.deviceOffset = 4096, .lengthBytes = 2048},
			 Extent{.deviceOffset = 65536, .lengthBytes = 1024}},
		.residentContent = {},
		.timestamps = Timestamps{.created = 11, .modified = 22, .accessed = 33},
		.confidence = Confidence::kValid,
		.source = CandidateSource::kFilesystem};
}

[[nodiscard]] Candidate carved() {
	return Candidate{
		.name = "jpg",
		.extents = {Extent{.deviceOffset = 999424, .lengthBytes = 2500}},
		.residentContent = {},
		.timestamps = Timestamps{.created = 0, .modified = 0, .accessed = 0},
		.confidence = Confidence::kUncertain,
		.source = CandidateSource::kCarve};
}

[[nodiscard]] Candidate residentEntry() {
	return Candidate{
		.name = "notes.txt",
		.extents = {},
		.residentContent = {std::byte{'h'}, std::byte{'i'}},
		.timestamps = Timestamps{.created = 1, .modified = 2, .accessed = 3},
		.confidence = Confidence::kValid,
		.source = CandidateSource::kFilesystem};
}

void appendAll(CandidateIndex& index, const std::vector<Candidate>& candidates) {
	for (const auto& candidate : candidates) {
		EXPECT_TRUE(index.append(candidate).hasValue());
	}
}

// Writes `candidates` into a fresh index in `directory`. `Result::value()`
// throws when the index could not be created, which is the loud failure this
// wants and one less branch than checking it.
void indexInto(const TempDir& directory, const std::vector<Candidate>& candidates) {
	auto index = CandidateIndex::create(directory.path());
	appendAll(index.value(), candidates);
}

[[nodiscard]] std::vector<std::byte> fileBytes(const std::filesystem::path& path) {
	std::ifstream stream{path, std::ios::binary};
	std::vector<std::byte> bytes;
	for (auto value = stream.get(); value != std::char_traits<char>::eof(); value = stream.get()) {
		bytes.push_back(std::bit_cast<std::byte>(static_cast<char>(value)));
	}
	return bytes;
}

void overwrite(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
	std::ofstream stream{path, std::ios::binary | std::ios::trunc};
	for (const std::byte value : bytes) {
		stream.put(std::bit_cast<char>(value));
	}
}

TEST(CandidateIndex, ReadsBackAnEmptyIndexAsEmpty) {
	TempDir directory;
	indexInto(directory, {});
	const auto contents = readIndex(directory.path());
	ASSERT_TRUE(contents.hasValue());
	EXPECT_TRUE(contents.value().candidates.empty());
	EXPECT_EQ(contents.value().droppedRecords, 0U);
}

TEST(CandidateIndex, RoundTripsEveryFieldOfANamedEntry) {
	TempDir directory;
	indexInto(directory, {namedEntry()});
	const auto contents = readIndex(directory.path());
	ASSERT_TRUE(contents.hasValue());
	ASSERT_EQ(contents.value().candidates.size(), 1U);
	const auto& read = contents.value().candidates.front();
	const auto expected = namedEntry();
	EXPECT_EQ(read.name, expected.name);
	EXPECT_EQ(read.extents.size(), expected.extents.size());
	EXPECT_EQ(read.extents.front().deviceOffset, expected.extents.front().deviceOffset);
	EXPECT_EQ(read.extents.back().lengthBytes, expected.extents.back().lengthBytes);
	EXPECT_EQ(read.timestamps.modified, expected.timestamps.modified);
	EXPECT_EQ(read.confidence, expected.confidence);
	EXPECT_EQ(read.source, expected.source);
}

TEST(CandidateIndex, RoundTripsResidentContentAndNoExtents) {
	TempDir directory;
	indexInto(directory, {residentEntry()});
	const auto contents = readIndex(directory.path());
	ASSERT_TRUE(contents.hasValue());
	ASSERT_EQ(contents.value().candidates.size(), 1U);
	EXPECT_TRUE(contents.value().candidates.front().extents.empty());
	EXPECT_EQ(contents.value().candidates.front().residentContent, residentEntry().residentContent);
}

TEST(CandidateIndex, CountsWhatItHasAppended) {
	TempDir directory;
	auto index = CandidateIndex::create(directory.path());
	ASSERT_TRUE(index.hasValue());
	EXPECT_EQ(index.value().count(), 0U);
	EXPECT_TRUE(index.value().append(namedEntry()).hasValue());
	EXPECT_EQ(index.value().count(), 1U);
}

TEST(CandidateIndex, KeepsAppendOrder) {
	TempDir directory;
	indexInto(directory, {namedEntry(), carved(), residentEntry()});
	const auto contents = readIndex(directory.path());
	ASSERT_TRUE(contents.hasValue());
	ASSERT_EQ(contents.value().candidates.size(), 3U);
	EXPECT_EQ(contents.value().candidates.at(0).name, "photos/holiday.jpg");
	EXPECT_EQ(contents.value().candidates.at(1).name, "jpg");
	EXPECT_EQ(contents.value().candidates.at(2).name, "notes.txt");
}

// An interrupted append leaves a readable prefix, not a corrupt file: the
// half-written record is dropped and counted, and everything before it stands.
TEST(CandidateIndex, DropsATornTailRecordAndKeepsThePrefix) {
	TempDir directory;
	indexInto(directory, {namedEntry(), carved()});
	auto records = fileBytes(directory.path() / kRecordFile);
	records.resize(records.size() - 8);
	overwrite(directory.path() / kRecordFile, records);
	const auto contents = readIndex(directory.path());
	ASSERT_TRUE(contents.hasValue());
	EXPECT_EQ(contents.value().candidates.size(), 1U);
	EXPECT_EQ(contents.value().droppedRecords, 1U);
}

// The blob is written before the record that points at it, so this can only
// happen to a damaged index — and it is refused rather than read as garbage.
TEST(CandidateIndex, DropsARecordPointingPastTheBlob) {
	TempDir directory;
	indexInto(directory, {namedEntry()});
	overwrite(directory.path() / "candidates.dat", {});
	const auto contents = readIndex(directory.path());
	ASSERT_TRUE(contents.hasValue());
	EXPECT_TRUE(contents.value().candidates.empty());
	EXPECT_EQ(contents.value().droppedRecords, 1U);
}

TEST(CandidateIndex, RefusesAFileThatIsNotAnIndex) {
	TempDir directory;
	indexInto(directory, {namedEntry()});
	auto records = fileBytes(directory.path() / kRecordFile);
	records.at(0) = std::byte{'X'};
	overwrite(directory.path() / kRecordFile, records);
	const auto contents = readIndex(directory.path());
	ASSERT_FALSE(contents.hasValue());
	EXPECT_EQ(contents.error().code, ErrorCode::kInvalidArgument);
}

TEST(CandidateIndex, RefusesAnIndexOfAnotherVersion) {
	TempDir directory;
	indexInto(directory, {namedEntry()});
	auto records = fileBytes(directory.path() / kRecordFile);
	records.at(8) = std::byte{0x7F};
	overwrite(directory.path() / kRecordFile, records);
	const auto contents = readIndex(directory.path());
	ASSERT_FALSE(contents.hasValue());
	EXPECT_EQ(contents.error().code, ErrorCode::kInvalidArgument);
}

TEST(CandidateIndex, RefusesADirectoryHoldingNoIndexAtAll) {
	TempDir directory;
	const auto contents = readIndex(directory.path());
	ASSERT_FALSE(contents.hasValue());
	EXPECT_EQ(contents.error().code, ErrorCode::kInvalidArgument);
}

// ADR-0009: a length the file states about itself may not size a read.
TEST(CandidateIndex, DropsARecordClaimingAnImpossibleName) {
	TempDir directory;
	indexInto(directory, {namedEntry()});
	auto records = fileBytes(directory.path() / kRecordFile);
	const auto nameLengthAt = 16 + 0x20;
	records.at(nameLengthAt) = std::byte{0xFF};
	records.at(nameLengthAt + 1) = std::byte{0xFF};
	overwrite(directory.path() / kRecordFile, records);
	const auto contents = readIndex(directory.path());
	ASSERT_TRUE(contents.hasValue());
	EXPECT_TRUE(contents.value().candidates.empty());
	EXPECT_EQ(contents.value().droppedRecords, 1U);
	EXPECT_GT(0xFFFFU, kMaxCandidateNameBytes);
}

// An interrupted run appended candidates past its last checkpoint that describe
// a region the resumed scan is about to read again. Dropping them is what keeps
// the index and the cursor saying the same thing.
TEST(CandidateIndex, ReopeningKeepsTheCheckpointedRecordsAndDropsTheTail) {
	const TempDir session;
	{
		auto index = CandidateIndex::create(session.path());
		ASSERT_TRUE(index.value().append(namedEntry()).hasValue());
		ASSERT_TRUE(index.value().append(carved()).hasValue());
		ASSERT_TRUE(index.value().append(residentEntry()).hasValue());
	}
	{
		auto reopened = CandidateIndex::reopen(session.path(), 1);
		ASSERT_TRUE(reopened.hasValue());
		EXPECT_EQ(reopened.value().count(), 1U);
	}
	const auto read = readIndex(session.path());
	ASSERT_TRUE(read.hasValue());
	ASSERT_EQ(read.value().candidates.size(), 1U);
	EXPECT_EQ(read.value().candidates.front().name, namedEntry().name);
}

TEST(CandidateIndex, AppendingAfterAReopenContinuesTheOrdinals) {
	const TempDir session;
	{
		auto index = CandidateIndex::create(session.path());
		ASSERT_TRUE(index.value().append(namedEntry()).hasValue());
		ASSERT_TRUE(index.value().append(carved()).hasValue());
	}
	{
		auto reopened = CandidateIndex::reopen(session.path(), 1);
		ASSERT_TRUE(reopened.hasValue());
		EXPECT_EQ(reopened.value().append(residentEntry()).value(), 1U);
	}
	const auto read = readIndex(session.path());
	ASSERT_EQ(read.value().candidates.size(), 2U);
	EXPECT_EQ(read.value().candidates.back().name, residentEntry().name);
}

// A checkpoint claiming more records than the index holds does not describe
// this index, so it is not something to continue from.
TEST(CandidateIndex, RefusesToReopenAtARecordItDoesNotHave) {
	const TempDir session;
	{
		auto index = CandidateIndex::create(session.path());
		ASSERT_TRUE(index.value().append(namedEntry()).hasValue());
	}
	const auto reopened = CandidateIndex::reopen(session.path(), 5);
	ASSERT_FALSE(reopened.hasValue());
	EXPECT_EQ(reopened.error().code, ErrorCode::kInvalidArgument);
}

} // namespace
