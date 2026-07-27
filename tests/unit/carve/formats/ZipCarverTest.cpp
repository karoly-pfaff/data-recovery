// SPDX-License-Identifier: GPL-3.0-or-later
#include "carve/formats/ZipCarver.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"

namespace {

using revenant::ByteReader;
using revenant::Confidence;
using revenant::carve::ZipCarver;

std::byte operator""_b(unsigned long long value) {
	return static_cast<std::byte>(value);
}

void appendLe16(std::vector<std::byte>& out, std::uint16_t value) {
	out.push_back(static_cast<std::byte>(value & 0xFFU));
	out.push_back(static_cast<std::byte>(value >> 8U));
}

void appendLe32(std::vector<std::byte>& out, std::uint32_t value) {
	appendLe16(out, static_cast<std::uint16_t>(value & 0xFFFFU));
	appendLe16(out, static_cast<std::uint16_t>(value >> 16U));
}

void appendAscii(std::vector<std::byte>& out, std::string_view text) {
	const auto raw = std::as_bytes(std::span{text.data(), text.size()});
	out.insert(out.end(), raw.begin(), raw.end());
}

// The three "PK" signatures, spelled as bytes rather than as escapes inside a
// string literal so the control characters stay visible in the source.
void appendSignature(std::vector<std::byte>& out, std::byte third, std::byte fourth) {
	out.push_back(0x50_b); // 'P'
	out.push_back(0x4B_b); // 'K'
	out.push_back(third);
	out.push_back(fourth);
}

// A structurally complete one-entry archive: local header + data, a central
// directory entry naming the file, and an EOCD whose arithmetic adds up.
struct ZipFixture {
	std::vector<std::byte> bytes;
	std::size_t directoryOffset = 0;
	std::size_t eocdOffset = 0;
};

void appendLocalEntry(std::vector<std::byte>& out, std::string_view name) {
	appendSignature(out, 0x03_b, 0x04_b);
	out.insert(out.end(), 26, 0x00_b); // the rest of the local header
	appendAscii(out, name);
	out.insert(out.end(), 16, 0x77_b); // entry data
}

void appendDirectoryEntry(std::vector<std::byte>& out, std::string_view name) {
	appendSignature(out, 0x01_b, 0x02_b);
	out.insert(out.end(), 42, 0x00_b); // the rest of the directory header
	appendAscii(out, name);
}

void appendEndRecord(
	std::vector<std::byte>& out,
	const ZipFixture& fixture,
	std::uint16_t comment) {
	appendSignature(out, 0x05_b, 0x06_b);
	appendLe16(out, 0); // this disk
	appendLe16(out, 0); // disk with the directory
	appendLe16(out, 1); // entries on this disk
	appendLe16(out, 1); // entries total
	appendLe32(out, static_cast<std::uint32_t>(fixture.eocdOffset - fixture.directoryOffset));
	appendLe32(out, static_cast<std::uint32_t>(fixture.directoryOffset));
	appendLe16(out, comment);
}

[[nodiscard]] ZipFixture makeZip(std::string_view name, std::uint16_t commentLength = 0) {
	ZipFixture fixture;
	appendLocalEntry(fixture.bytes, name);
	fixture.directoryOffset = fixture.bytes.size();
	appendDirectoryEntry(fixture.bytes, name);
	fixture.eocdOffset = fixture.bytes.size();
	appendEndRecord(fixture.bytes, fixture, commentLength);
	return fixture;
}

TEST(ZipCarver, ValidArchiveYieldsExactLengthAndValid) {
	const auto zip = makeZip("hello.txt");
	ByteReader reader{zip.bytes};
	const auto result = ZipCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, zip.bytes.size());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
	EXPECT_EQ(result.value().extension, "zip");
}

TEST(ZipCarver, ExtentStopsAtTheEndRecordDespiteTrailingGarbage) {
	auto zip = makeZip("hello.txt");
	const auto realSize = zip.bytes.size();
	zip.bytes.resize(realSize + 300, 0xEE_b);
	ByteReader reader{zip.bytes};
	const auto result = ZipCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, realSize); // THE anti-false-positive assertion
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

[[nodiscard]] std::string extensionOf(std::string_view entryName) {
	const auto zip = makeZip(entryName);
	ByteReader reader{zip.bytes};
	return ZipCarver{}.carve(reader).value().extension;
}

TEST(ZipCarver, OfficeArchivesAreNamedByTheirEntryPaths) {
	EXPECT_EQ(extensionOf("word/document.xml"), "docx");
	EXPECT_EQ(extensionOf("xl/workbook.xml"), "xlsx");
	EXPECT_EQ(extensionOf("ppt/presentation.xml"), "pptx");
}

TEST(ZipCarver, AnEndRecordWhoseDirectoryDoesNotAddUpIsUncertain) {
	auto zip = makeZip("hello.txt");
	// Corrupt the recorded central-directory offset.
	zip.bytes.at(zip.eocdOffset + 16) = 0xFF_b;
	ByteReader reader{zip.bytes};
	const auto result = ZipCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
}

TEST(ZipCarver, TheLastEndRecordWinsOverAStrayOneInTheData) {
	auto zip = makeZip("hello.txt");
	// Plant a stray EOCD signature inside the entry data, before the real one.
	zip.bytes.at(32) = 0x50_b;
	zip.bytes.at(33) = 0x4B_b;
	zip.bytes.at(34) = 0x05_b;
	zip.bytes.at(35) = 0x06_b;
	ByteReader reader{zip.bytes};
	const auto result = ZipCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, zip.bytes.size());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(ZipCarver, ACommentRunningPastTheDataIsUncertainAndBounded) {
	const auto zip = makeZip("hello.txt", 500);
	ByteReader reader{zip.bytes};
	const auto result = ZipCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_EQ(result.value().length, zip.bytes.size());
}

TEST(ZipCarver, AnArchiveWithoutAnEndRecordIsRejected) {
	auto zip = makeZip("hello.txt");
	zip.bytes.resize(zip.eocdOffset); // cut the EOCD off entirely
	ByteReader reader{zip.bytes};
	const auto result = ZipCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
	EXPECT_EQ(result.value().length, 0U);
}

TEST(ZipCarver, NonZipBytesAreRejected) {
	const std::vector<std::byte> bytes(64, 0x5A_b);
	ByteReader reader{bytes};
	const auto result = ZipCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
}

TEST(ZipCarver, EmptyInputIsRejected) {
	const std::vector<std::byte> bytes;
	ByteReader reader{bytes};
	const auto result = ZipCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
}

TEST(ZipCarver, SignatureIsTheLocalFileHeaderAtOffsetZero) {
	const auto signatures = ZipCarver{}.signatures();
	ASSERT_EQ(signatures.size(), 1U);
	EXPECT_EQ(signatures.front().offset, 0U);
	EXPECT_EQ(signatures.front().magic.size(), 4U);
}

} // namespace
