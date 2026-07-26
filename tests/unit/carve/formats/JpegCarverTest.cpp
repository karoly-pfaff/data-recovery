// SPDX-License-Identifier: GPL-3.0-or-later
#include "carve/formats/JpegCarver.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"

namespace {

using revenant::ByteReader;
using revenant::Confidence;
using revenant::carve::JpegCarver;

std::byte operator""_b(unsigned long long value) {
	return static_cast<std::byte>(value);
}

// Minimal STRUCTURALLY valid JPEG (22 bytes): SOI, one APP0-style segment,
// SOS with an empty header, entropy data exercising byte-stuffing and a
// restart marker, EOI. The carver validates structure, not decodability.
std::vector<std::byte> minimalJpeg() {
	return {
		0xFF_b, 0xD8_b,                                 // SOI
		0xFF_b, 0xE0_b, 0x00_b, 0x04_b, 0x4A_b, 0x46_b, // APP0, len 4, "JF"
		0xFF_b, 0xDA_b, 0x00_b, 0x02_b,                 // SOS, len 2 (empty)
		0x01_b, 0xFF_b, 0x00_b, 0x02_b,                 // entropy: data, stuffed FF, data
		0xFF_b, 0xD3_b, 0x03_b, 0x04_b,                 // RST3, more data
		0xFF_b, 0xD9_b,                                 // EOI
	};
}

TEST(JpegCarver, ValidJpegYieldsExactLengthAndValid) {
	const auto bytes = minimalJpeg();
	ByteReader reader{bytes};
	const auto result = JpegCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, bytes.size());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
	EXPECT_EQ(result.value().extension, "jpg");
}

TEST(JpegCarver, ExtentStopsAtEoiDespiteTrailingGarbage) {
	auto bytes = minimalJpeg();
	const auto realSize = bytes.size();
	bytes.resize(realSize + 100, 0xEE_b); // trailing garbage, incl. fake FFD8 below
	bytes.at(realSize + 10) = 0xFF_b;
	bytes.at(realSize + 11) = 0xD8_b;
	ByteReader reader{bytes};
	const auto result = JpegCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, realSize); // THE anti-false-positive assertion
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(JpegCarver, TruncatedAfterSosIsUncertainAndBounded) {
	auto bytes = minimalJpeg();
	bytes.resize(bytes.size() - 4); // cut mid-entropy, before EOI
	ByteReader reader{bytes};
	const auto result = JpegCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	// Every remaining byte was confirmed entropy data (plain byte, stuffed FF
	// 00, RST3 pair) right up to the cut — the exhausted read is discovered
	// exactly at the buffer's end, so the exact extent is the whole input.
	EXPECT_EQ(result.value().length, bytes.size());
	EXPECT_LE(result.value().length, bytes.size()); // never overruns
}

TEST(JpegCarver, TruncatedBeforeSosIsRejected) {
	std::vector<std::byte> bytes{0xFF_b, 0xD8_b, 0xFF_b, 0xE0_b, 0x00_b};
	ByteReader reader{bytes};
	const auto result = JpegCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
}

TEST(JpegCarver, NonJpegBytesAreRejected) {
	std::vector<std::byte> bytes(64, 0x11_b);
	ByteReader reader{bytes};
	const auto result = JpegCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
	EXPECT_EQ(result.value().length, 0U);
}

TEST(JpegCarver, SegmentLengthBelowTwoIsStructurallyInvalid) {
	std::vector<std::byte>
		bytes{0xFF_b, 0xD8_b, 0xFF_b, 0xE0_b, 0x00_b, 0x01_b, 0xAA_b}; // len 1 < 2
	ByteReader reader{bytes};
	const auto result = JpegCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
}

TEST(JpegCarver, OversizedSegmentLengthBeforeSosIsRejected) {
	// APP0 declares length 0xFFFF, far past this 6-byte buffer.
	std::vector<std::byte> bytes{0xFF_b, 0xD8_b, 0xFF_b, 0xE0_b, 0xFF_b, 0xFF_b};
	ByteReader reader{bytes};
	const auto result = JpegCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
	EXPECT_EQ(result.value().length, 0U);
}

TEST(JpegCarver, OversizedSegmentLengthAfterSosIsUncertainAndBounded) {
	// SOI, SOS (empty header), one entropy byte, a DHT marker (0xC4) that
	// terminates entropy scanning, then DHT declares length 0xFFFF — far
	// past this 11-byte buffer.
	std::vector<std::byte> bytes{
		0xFF_b,
		0xD8_b, // SOI
		0xFF_b,
		0xDA_b,
		0x00_b,
		0x02_b, // SOS, len 2 (empty)
		0x01_b, // entropy: one data byte
		0xFF_b,
		0xC4_b, // DHT marker: terminates entropy scan
		0xFF_b,
		0xFF_b}; // DHT declares length 0xFFFF (oversized)
	ByteReader reader{bytes};
	const auto result = JpegCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	// Exact per hand-trace: SOI(2) -> SOS marker+len (pos 2 -> 6) -> entropy
	// byte 0x01 (pos 6 -> 7) -> FF at 7 is a genuine terminator (code at 8 =
	// 0xC4, neither stuffed-zero nor RST) so entropy stops at pos 7 -> the
	// next marker read consumes the DHT marker pair (pos 7 -> 9) -> its
	// declared length (0xFFFF) starting at pos 9 would end at 65544, past
	// this 11-byte buffer, so the segment is rejected with pos left at 9.
	EXPECT_EQ(result.value().length, 9U);
	EXPECT_LE(result.value().length, bytes.size());
}

TEST(JpegCarver, FillBytesBeforeMarkerCodeAreLegal) {
	std::vector<std::byte> bytes{
		0xFF_b,
		0xD8_b, // SOI
		0xFF_b,
		0xFF_b,
		0xFF_b,
		0xDA_b,
		0x00_b,
		0x02_b, // fill FFs, then SOS
		0x01_b,
		0xFF_b,
		0xD9_b}; // EOI
	ByteReader reader{bytes};
	const auto result = JpegCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
	EXPECT_EQ(result.value().length, bytes.size());
}

TEST(JpegCarver, TwoSosScansSeparatedByDhtAreValid) {
	// Progressive-JPEG shape: SOS #1 -> entropy terminated by a DHT segment
	// (not EOI) -> SOS #2 -> entropy -> EOI. Hand-trace: SOI (pos 0->2); SOS1
	// marker+len2 (pos 2->6); entropy byte 0x01 (pos 6->7); FF at 7 is a
	// terminator (code at 8 = 0xC4, DHT) so entropy stops at pos 7; DHT
	// marker+len4 (pos 7->9 code read, len field at 9 = 4 incl. 2 payload
	// bytes -> pos 13); SOS2 marker+len2 (pos 13->17); entropy byte 0x02
	// (pos 17->18); FF at 18 is EOI (code at 19 = 0xD9) so entropy stops at
	// pos 18; EOI marker consumed (pos 18->20). End pos 20 == fixture size.
	std::vector<std::byte> bytes{0xFF_b, 0xD8_b,                 // SOI
								 0xFF_b, 0xDA_b, 0x00_b, 0x02_b, // SOS #1, len 2 (empty)
								 0x01_b,                         // entropy #1: one data byte
								 0xFF_b, 0xC4_b, 0x00_b, 0x04_b,
								 0xAA_b, 0xBB_b,                 // DHT, len 4, 2 payload bytes
								 0xFF_b, 0xDA_b, 0x00_b, 0x02_b, // SOS #2, len 2 (empty)
								 0x02_b,                         // entropy #2: one data byte
								 0xFF_b, 0xD9_b};                // EOI
	ByteReader reader{bytes};
	const auto result = JpegCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
	EXPECT_EQ(result.value().length, bytes.size());
}

TEST(JpegCarver, SignatureIsSoiPrefix) {
	const JpegCarver carver;
	ASSERT_EQ(carver.signatures().size(), 1U);
	EXPECT_EQ(carver.signatures().front().magic.size(), 3U);
	EXPECT_EQ(carver.signatures().front().offset, 0U);
}

} // namespace
