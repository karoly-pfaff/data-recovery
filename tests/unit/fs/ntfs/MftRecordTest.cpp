// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/MftRecord.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/core/Error.hpp"
#include "support/MftRecordTestSupport.hpp"

namespace {

using mft_record_test_support::kDataAttributeOffset;
using mft_record_test_support::kFileNameAttributeOffset;
using mft_record_test_support::makeValidRecord;
using revenant::Confidence;
using revenant::ErrorCode;
using revenant::fs::ntfs::parseMftRecord;

void fillSpan(std::span<std::byte> target, std::byte value) {
	for (auto& b : target) {
		b = value;
	}
}

void writeLe16(std::vector<std::byte>& record, std::size_t offset, std::uint16_t value) {
	record.at(offset) = static_cast<std::byte>(value & 0xFFU);
	record.at(offset + 1) = static_cast<std::byte>(value >> 8U);
}

void writeLe32(std::vector<std::byte>& record, std::size_t offset, std::uint32_t value) {
	writeLe16(record, offset, static_cast<std::uint16_t>(value & 0xFFFFU));
	writeLe16(record, offset + 2, static_cast<std::uint16_t>(value >> 16U));
}

[[nodiscard]] std::string toString(const std::vector<std::byte>& bytes) {
	std::string out;
	out.reserve(bytes.size());
	for (const std::byte b : bytes) {
		out.push_back(static_cast<char>(std::to_integer<unsigned char>(b)));
	}
	return out;
}

TEST(MftRecord, ParsesRecordHeader) {
	const auto record = makeValidRecord();
	const auto result = parseMftRecord(record, 42);
	ASSERT_TRUE(result.hasValue());
	const auto& view = result.value();
	EXPECT_EQ(view.recordNumber, 42U);
	EXPECT_TRUE(view.inUse);
	EXPECT_FALSE(view.isDirectory);
	EXPECT_EQ(view.sequence, 1U);
	EXPECT_EQ(view.grade, Confidence::kValid);
}

TEST(MftRecord, ParsesStandardInformation) {
	const auto record = makeValidRecord();
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	const auto& view = result.value();
	ASSERT_TRUE(view.standardInfo.has_value());
	// NOLINTNEXTLINE(bugprone-unchecked-optional-access) - checked immediately above.
	const auto& info = view.standardInfo.value();
	EXPECT_EQ(info.created, 0x1111U);
	EXPECT_EQ(info.modified, 0x2222U);
	EXPECT_EQ(info.accessed, 0x4444U);
}

TEST(MftRecord, ParsesFileName) {
	const auto record = makeValidRecord();
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	const auto& view = result.value();
	ASSERT_EQ(view.names.size(), 1U);
	const auto& name = view.names.front();
	EXPECT_EQ(name.parentRecord, 5U);
	EXPECT_EQ(name.parentSequence, 1U);
	EXPECT_EQ(name.nameSpace, 1U);
	EXPECT_EQ(name.name.utf8, "photo.jpg");
	EXPECT_EQ(name.realSize, 0U);
}

TEST(MftRecord, ParsesResidentData) {
	const auto record = makeValidRecord();
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	const auto& view = result.value();
	ASSERT_TRUE(view.data.has_value());
	// NOLINTNEXTLINE(bugprone-unchecked-optional-access) - checked immediately above.
	const auto& data = view.data.value();
	EXPECT_TRUE(data.resident);
	EXPECT_EQ(toString(data.residentContent), "hello-ntfs");
}

TEST(MftRecord, DeletedFlagIsNotInUse) {
	auto record = makeValidRecord();
	const std::span<std::byte> target{record};
	target.subspan(0x16, 2).front() = std::byte{0};
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	EXPECT_FALSE(result.value().inUse);
}

TEST(MftRecord, DirectoryFlagIsDetected) {
	auto record = makeValidRecord();
	const std::span<std::byte> target{record};
	target.subspan(0x16, 2).front() = static_cast<std::byte>(0x03);
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().isDirectory);
}

TEST(MftRecord, TornFixupMarksUncertain) {
	auto record = makeValidRecord();
	record.at(0x1FE) = std::byte{0x00};
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().grade, Confidence::kUncertain);
	EXPECT_FALSE(result.value().standardInfo.has_value());
	EXPECT_TRUE(result.value().names.empty());
}

TEST(MftRecord, BadSignatureReturnsNotFound) {
	std::vector<std::byte> record = makeValidRecord();
	record.at(0) = std::byte{'X'};
	const auto result = parseMftRecord(record, 1);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kNotFound);
}

TEST(MftRecord, ExtensionRecordReturnsUncertainShell) {
	auto record = makeValidRecord();
	const std::span<std::byte> target{record};
	fillSpan(target.subspan(0x20, 8), std::byte{0x01});
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().grade, Confidence::kUncertain);
	EXPECT_FALSE(result.value().standardInfo.has_value());
}

TEST(MftRecord, OverDeclaredAttributeLengthStopsParsingUncertain) {
	auto record = makeValidRecord();
	const std::span<std::byte> target{record};
	fillSpan(target.subspan(0x3C, 4), std::byte{0xFF});
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().grade, Confidence::kUncertain);
}

TEST(MftRecord, FileNameNameLengthOverrunDropsNameButKeepsRecordUncertain) {
	auto record = makeValidRecord();
	const std::span<std::byte> target{record};
	// Attribute start + resident content (0x18) + $FILE_NAME name-length (0x40).
	target.subspan(kFileNameAttributeOffset + 0x58, 1).front() = std::byte{0xFF};
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().names.empty());
	EXPECT_EQ(result.value().grade, Confidence::kUncertain);
}

TEST(MftRecord, UnknownAttributeTypeIsSkipped) {
	auto record = makeValidRecord();
	const std::span<std::byte> target{record};
	target.subspan(0x38, 4).front() = std::byte{0x40};
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().grade, Confidence::kValid);
}

// A first-attribute offset within four bytes of the record end left no room for
// the attribute type code. The out-of-range read was reported by ByteReader but
// discarded by an unchecked Result::value(), so the kOutOfRange turned into an
// escaping std::bad_variant_access instead of a graded record.
TEST(MftRecord, AttributeOffsetAtRecordTailIsGradedNotThrown) {
	auto record = makeValidRecord();
	writeLe16(record, 0x14, static_cast<std::uint16_t>(record.size() - 2));
	writeLe32(record, 0x18, static_cast<std::uint32_t>(record.size()));
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().grade, Confidence::kUncertain);
}

// The update-sequence array must hold one entry per 512-byte stride plus the
// USN itself. A short count used to pass validation, leaving the strides it did
// not cover holding their on-disk USN placeholder while the record still graded
// kValid — silent corruption in recovered content.
TEST(MftRecord, UpdateSequenceCountMustCoverEveryStride) {
	auto record = makeValidRecord();
	writeLe16(record, 0x06, 2);
	const auto result = parseMftRecord(record, 1);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// The end marker stops attribute parsing without advancing the walk offset, so
// reporting it as ordinary success made the caller re-read the same marker for
// ever whenever it sat below usedSize. Found by the seeded fuzzer: a hang, not a
// crash, which wedges a whole device scan. If this regresses, the test suite
// stops here rather than failing.
TEST(MftRecord, EndMarkerBelowUsedSizeTerminatesParsing) {
	auto record = makeValidRecord();
	writeLe32(record, kDataAttributeOffset, 0xFFFFFFFFU);
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	EXPECT_FALSE(result.value().data.has_value());
	EXPECT_EQ(result.value().grade, Confidence::kValid);
}

// A resident content length chosen to wrap the header's 32-bit bounds check
// must still be rejected, dropping $DATA and grading the record uncertain.
TEST(MftRecord, WrappingResidentContentLengthDropsData) {
	auto record = makeValidRecord();
	writeLe32(record, kDataAttributeOffset + 0x10, 0xFFFFFFF0U);
	const auto result = parseMftRecord(record, 1);
	ASSERT_TRUE(result.hasValue());
	EXPECT_FALSE(result.value().data.has_value());
	EXPECT_EQ(result.value().grade, Confidence::kUncertain);
}

} // namespace
