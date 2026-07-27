// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/MftRecordBuilder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "imagegen/ntfs/AttributeBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"

namespace {

using revenant::ByteReader;
using revenant::Confidence;
using revenant::fs::ntfs::parseMftRecord;
using revenant::imagegen::ntfs::buildFileName;
using revenant::imagegen::ntfs::buildMftRecord;
using revenant::imagegen::ntfs::buildResidentData;
using revenant::imagegen::ntfs::buildStandardInformation;
using revenant::imagegen::ntfs::FileNameSpec;
using revenant::imagegen::ntfs::kUpdateSequenceNumber;
using revenant::imagegen::ntfs::makeLayout;
using revenant::imagegen::ntfs::MftRecordSpec;

constexpr std::size_t kFirstStrideTail = 510;
constexpr std::size_t kSecondStrideTail = 1022;

// A payload long enough to reach past the first stride boundary, so the update
// sequence array has real content bytes to save rather than zeros.
[[nodiscard]] std::vector<std::byte> longPayload() {
	std::vector<std::byte> content(600, std::byte{0});
	for (std::size_t i = 0; i < content.size(); ++i) {
		content.at(i) = static_cast<std::byte>((i % 251) + 1);
	}
	return content;
}

[[nodiscard]] std::vector<std::byte> attributesFor(const std::vector<std::byte>& content) {
	std::vector<std::byte> out;
	for (const auto& attribute :
		 {buildStandardInformation(),
		  buildFileName(
			  FileNameSpec{
				  .parentRecord = 5,
				  .parentSequence = 1,
				  .name = "notes.txt",
				  .realSize = 600}),
		  buildResidentData(content),
		  revenant::imagegen::ntfs::buildEndMarker()}) {
		out.insert(out.end(), attribute.begin(), attribute.end());
	}
	return out;
}

[[nodiscard]] std::uint16_t le16At(const std::vector<std::byte>& record, std::size_t offset) {
	return ByteReader{record}.readLe<std::uint16_t>(offset).value();
}

[[nodiscard]] std::vector<std::byte> buildTestRecord(bool inUse, bool isDirectory) {
	const auto content = longPayload();
	const auto attributes = attributesFor(content);
	return buildMftRecord(
		makeLayout(),
		MftRecordSpec{
			.sequence = 3,
			.inUse = inUse,
			.isDirectory = isDirectory,
			.attributes = attributes});
}

TEST(MftRecordBuilder, ProducesOneRecordSizedRecord) {
	EXPECT_EQ(buildTestRecord(true, false).size(), makeLayout().mftRecordBytes);
}

TEST(MftRecordBuilder, ParsesBackAsAValidRecord) {
	const auto record = buildTestRecord(true, false);
	const auto parsed = parseMftRecord(record, 19);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_EQ(parsed.value().grade, Confidence::kValid);
	EXPECT_EQ(parsed.value().recordNumber, 19U);
	EXPECT_EQ(parsed.value().sequence, 3U);
	EXPECT_TRUE(parsed.value().inUse);
	EXPECT_FALSE(parsed.value().isDirectory);
}

TEST(MftRecordBuilder, CarriesTheDeletedAndDirectoryFlags) {
	const auto deleted = parseMftRecord(buildTestRecord(false, false), 1);
	ASSERT_TRUE(deleted.hasValue());
	EXPECT_FALSE(deleted.value().inUse);
	const auto directory = parseMftRecord(buildTestRecord(true, true), 2);
	ASSERT_TRUE(directory.hasValue());
	EXPECT_TRUE(directory.value().isDirectory);
}

TEST(MftRecordBuilder, StampsTheUpdateSequenceNumberIntoEveryStrideTail) {
	const auto record = buildTestRecord(true, false);
	EXPECT_EQ(le16At(record, kFirstStrideTail), kUpdateSequenceNumber);
	EXPECT_EQ(le16At(record, kSecondStrideTail), kUpdateSequenceNumber);
}

TEST(MftRecordBuilder, TheFixupRestoresTheContentTheStrideTailOverwrote) {
	const auto content = longPayload();
	const auto record = buildTestRecord(true, false);
	const auto parsed = parseMftRecord(record, 19);
	ASSERT_TRUE(parsed.hasValue());
	ASSERT_TRUE(parsed.value().data.has_value());
	// NOLINTNEXTLINE(bugprone-unchecked-optional-access) - checked immediately above.
	EXPECT_EQ(parsed.value().data.value().residentContent, content);
}

TEST(MftRecordBuilder, ParsesTheAttributesItWasGiven) {
	const auto parsed = parseMftRecord(buildTestRecord(true, false), 19);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_TRUE(parsed.value().standardInfo.has_value());
	ASSERT_EQ(parsed.value().names.size(), 1U);
	EXPECT_EQ(parsed.value().names.front().name.utf8, "notes.txt");
	EXPECT_EQ(parsed.value().names.front().parentRecord, 5U);
}

TEST(MftRecordBuilder, IsDeterministic) {
	EXPECT_EQ(buildTestRecord(true, false), buildTestRecord(true, false));
}

} // namespace
