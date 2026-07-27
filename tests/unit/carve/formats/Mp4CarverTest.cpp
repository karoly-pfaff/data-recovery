// SPDX-License-Identifier: GPL-3.0-or-later
#include "carve/formats/Mp4Carver.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"

namespace {

using revenant::ByteReader;
using revenant::Confidence;
using revenant::carve::Mp4Carver;

std::byte operator""_b(unsigned long long value) {
	return static_cast<std::byte>(value);
}

void appendBe32(std::vector<std::byte>& out, std::uint32_t value) {
	for (int shift = 24; shift >= 0; shift -= 8) {
		out.push_back(static_cast<std::byte>((value >> static_cast<std::uint32_t>(shift)) & 0xFFU));
	}
}

void appendAscii(std::vector<std::byte>& out, std::string_view text) {
	const auto raw = std::as_bytes(std::span{text.data(), text.size()});
	out.insert(out.end(), raw.begin(), raw.end());
}

// One top-level box: 32-bit size, 4-char type, payload.
void appendBox(
	std::vector<std::byte>& out,
	std::string_view type,
	const std::vector<std::byte>& payload) {
	appendBe32(out, static_cast<std::uint32_t>(8 + payload.size()));
	appendAscii(out, type);
	out.insert(out.end(), payload.begin(), payload.end());
}

[[nodiscard]] std::vector<std::byte> ftypPayload(std::string_view brand) {
	std::vector<std::byte> payload;
	appendAscii(payload, brand);  // major brand
	appendBe32(payload, 512);     // minor version
	appendAscii(payload, "mp41"); // one compatible brand
	return payload;
}

[[nodiscard]] std::vector<std::byte> minimalMp4(std::string_view brand = "isom") {
	std::vector<std::byte> out;
	appendBox(out, "ftyp", ftypPayload(brand));
	appendBox(out, "moov", std::vector<std::byte>(24, 0x11_b));
	appendBox(out, "mdat", std::vector<std::byte>(64, 0x22_b));
	return out;
}

TEST(Mp4Carver, ValidFileYieldsExactLengthAndValid) {
	const auto bytes = minimalMp4();
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, bytes.size());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
	EXPECT_EQ(result.value().extension, "mp4");
}

TEST(Mp4Carver, QuickTimeBrandCarvesAsMov) {
	const auto bytes = minimalMp4("qt  ");
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().extension, "mov");
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(Mp4Carver, ExtentStopsAtTheLastBoxDespiteTrailingGarbage) {
	auto bytes = minimalMp4();
	const auto realSize = bytes.size();
	bytes.resize(realSize + 200, 0xEE_b);
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, realSize); // THE anti-false-positive assertion
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(Mp4Carver, WalksTheSixtyFourBitLargesizeForm) {
	std::vector<std::byte> bytes;
	appendBox(bytes, "ftyp", ftypPayload("isom"));
	appendBox(bytes, "moov", std::vector<std::byte>(8, 0x11_b));
	// mdat in the size==1 form: header is 16 bytes, payload 32.
	appendBe32(bytes, 1);
	appendAscii(bytes, "mdat");
	appendBe32(bytes, 0);
	appendBe32(bytes, 48);
	bytes.insert(bytes.end(), 32, 0x33_b);
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, bytes.size());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(Mp4Carver, TruncatedFinalBoxIsUncertainAndBounded) {
	auto bytes = minimalMp4();
	bytes.resize(bytes.size() - 20);
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_LT(result.value().length, bytes.size());
	EXPECT_GT(result.value().length, 0U);
}

TEST(Mp4Carver, ASizeZeroBoxStopsTheWalkBecauseItsExtentIsUnknowable) {
	std::vector<std::byte> bytes;
	appendBox(bytes, "ftyp", ftypPayload("isom"));
	const auto afterFtyp = bytes.size();
	appendBe32(bytes, 0); // "to end of file"
	appendAscii(bytes, "mdat");
	bytes.insert(bytes.end(), 40, 0x44_b);
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_EQ(result.value().length, afterFtyp);
}

TEST(Mp4Carver, ASizeBelowTheHeaderStopsTheWalk) {
	std::vector<std::byte> bytes;
	appendBox(bytes, "ftyp", ftypPayload("isom"));
	const auto afterFtyp = bytes.size();
	appendBe32(bytes, 4); // smaller than the 8-byte header
	appendAscii(bytes, "moov");
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_EQ(result.value().length, afterFtyp);
}

TEST(Mp4Carver, ANonPrintableBoxTypeStopsTheWalk) {
	auto bytes = minimalMp4();
	const auto moovTypeOffset = 20U + 4U; // ftyp box is 20 bytes; skip the size field
	bytes.at(moovTypeOffset) = 0x00_b;
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_EQ(result.value().length, 20U);
}

TEST(Mp4Carver, BytesWithoutFtypFirstAreRejected) {
	std::vector<std::byte> bytes;
	appendBox(bytes, "moov", std::vector<std::byte>(8, 0x11_b));
	appendBox(bytes, "mdat", std::vector<std::byte>(8, 0x22_b));
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
	EXPECT_EQ(result.value().length, 0U);
}

TEST(Mp4Carver, AFileWithoutMdatIsUncertain) {
	std::vector<std::byte> bytes;
	appendBox(bytes, "ftyp", ftypPayload("isom"));
	appendBox(bytes, "moov", std::vector<std::byte>(16, 0x11_b));
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_EQ(result.value().length, bytes.size());
}

TEST(Mp4Carver, EmptyInputIsRejected) {
	const std::vector<std::byte> bytes;
	ByteReader reader{bytes};
	const auto result = Mp4Carver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
}

TEST(Mp4Carver, SignatureIsFtypAtOffsetFour) {
	const auto signatures = Mp4Carver{}.signatures();
	ASSERT_EQ(signatures.size(), 1U);
	EXPECT_EQ(signatures.front().offset, 4U);
	EXPECT_EQ(signatures.front().magic.size(), 4U);
}

} // namespace
