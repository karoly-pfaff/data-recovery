// SPDX-License-Identifier: GPL-3.0-or-later
#include "carve/formats/PngCarver.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Crc32.hpp"

namespace {

using revenant::ByteReader;
using revenant::Confidence;
using revenant::crc32;
using revenant::carve::PngCarver;

std::byte operator""_b(unsigned long long value) {
	return static_cast<std::byte>(value);
}

void appendBe32(std::vector<std::byte>& out, std::uint32_t value) {
	for (int shift = 24; shift >= 0; shift -= 8) {
		out.push_back(static_cast<std::byte>((value >> static_cast<std::uint32_t>(shift)) & 0xFFU));
	}
}

// One chunk: length, type, data, and the CRC over type+data. The CRC comes
// from the production primitive, which Crc32Test pins to published vectors
// independently — so a wrong CRC here cannot silently agree with a wrong one
// in the carver.
void appendChunk(
	std::vector<std::byte>& out,
	std::string_view type,
	const std::vector<std::byte>& data) {
	appendBe32(out, static_cast<std::uint32_t>(data.size()));
	std::vector<std::byte> covered;
	const auto typeBytes = std::as_bytes(std::span{type.data(), type.size()});
	covered.insert(covered.end(), typeBytes.begin(), typeBytes.end());
	covered.insert(covered.end(), data.begin(), data.end());
	out.insert(out.end(), covered.begin(), covered.end());
	appendBe32(out, crc32(covered));
}

[[nodiscard]] std::vector<std::byte> pngSignature() {
	return {0x89_b, 0x50_b, 0x4E_b, 0x47_b, 0x0D_b, 0x0A_b, 0x1A_b, 0x0A_b};
}

// Signature + IHDR (13 bytes, as the spec fixes it) + one IDAT + IEND.
[[nodiscard]] std::vector<std::byte> minimalPng() {
	auto out = pngSignature();
	appendChunk(out, "IHDR", std::vector<std::byte>(13, 0x01_b));
	appendChunk(out, "IDAT", std::vector<std::byte>(20, 0x77_b));
	appendChunk(out, "IEND", {});
	return out;
}

TEST(PngCarver, ValidPngYieldsExactLengthAndValid) {
	const auto bytes = minimalPng();
	ByteReader reader{bytes};
	const auto result = PngCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, bytes.size());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
	EXPECT_EQ(result.value().extension, "png");
}

TEST(PngCarver, ExtentStopsAtIendDespiteTrailingGarbage) {
	auto bytes = minimalPng();
	const auto realSize = bytes.size();
	const auto second = minimalPng(); // a whole second PNG glued on
	bytes.insert(bytes.end(), second.begin(), second.end());
	ByteReader reader{bytes};
	const auto result = PngCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, realSize); // THE anti-false-positive assertion
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(PngCarver, TruncatedMidChunkIsUncertainAndBounded) {
	auto bytes = minimalPng();
	bytes.resize(bytes.size() - 10);
	ByteReader reader{bytes};
	const auto result = PngCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_LE(result.value().length, bytes.size());
	EXPECT_GT(result.value().length, 0U);
}

TEST(PngCarver, ACorruptedChunkCrcStopsTheWalk) {
	auto bytes = minimalPng();
	constexpr std::size_t kIdatDataStart = 8 + 12 + 13 + 8;
	bytes.at(kIdatDataStart) = 0xFF_b; // flip a byte the IDAT CRC covers
	ByteReader reader{bytes};
	const auto result = PngCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	// Bounded at the end of IHDR: the signature plus one complete chunk.
	EXPECT_EQ(result.value().length, 8U + 12U + 13U);
}

TEST(PngCarver, AFirstChunkThatIsNotIhdrIsRejected) {
	auto bytes = pngSignature();
	appendChunk(bytes, "IDAT", std::vector<std::byte>(4, 0x00_b));
	appendChunk(bytes, "IEND", {});
	ByteReader reader{bytes};
	const auto result = PngCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
	EXPECT_EQ(result.value().length, 0U);
}

TEST(PngCarver, AnAbsurdChunkLengthIsBoundedNotRead) {
	auto bytes = minimalPng();
	constexpr std::size_t kIdatLengthOffset = 8 + 12 + 13;
	bytes.at(kIdatLengthOffset) = 0x7F_b; // ~2 GiB, far past the buffer
	bytes.at(kIdatLengthOffset + 1) = 0xFF_b;
	ByteReader reader{bytes};
	const auto result = PngCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_EQ(result.value().length, 8U + 12U + 13U);
}

TEST(PngCarver, NonPngBytesAreRejected) {
	const std::vector<std::byte> bytes(64, 0x5A_b);
	ByteReader reader{bytes};
	const auto result = PngCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
	EXPECT_EQ(result.value().length, 0U);
}

TEST(PngCarver, EmptyInputIsRejected) {
	const std::vector<std::byte> bytes;
	ByteReader reader{bytes};
	const auto result = PngCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
}

TEST(PngCarver, SignatureIsTheEightBytePngMagicAtOffsetZero) {
	const auto signatures = PngCarver{}.signatures();
	ASSERT_EQ(signatures.size(), 1U);
	EXPECT_EQ(signatures.front().offset, 0U);
	EXPECT_EQ(signatures.front().magic.size(), 8U);
}

} // namespace
