// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/Utf16Name.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace {

using revenant::decodeUtf16Name;

// Builds a UTF-16LE byte buffer from a list of 16-bit code units — the
// on-disk form `decodeUtf16Name` consumes.
std::vector<std::byte> utf16LeBytes(std::initializer_list<std::uint16_t> units) {
	std::vector<std::byte> bytes;
	bytes.reserve(units.size() * 2);
	for (const std::uint16_t unit : units) {
		bytes.push_back(static_cast<std::byte>(unit & 0xFFU));
		bytes.push_back(static_cast<std::byte>((unit >> 8U) & 0xFFU));
	}
	return bytes;
}

TEST(NameDecode, EmptyInputDecodesToEmptyLosslessString) {
	const auto result = decodeUtf16Name(std::span<const std::byte>{});
	EXPECT_EQ(result.utf8, "");
	EXPECT_TRUE(result.lossless);
}

TEST(NameDecode, AsciiRoundTrips) {
	const auto bytes = utf16LeBytes({'r', 'e', 'p', 'o', 'r', 't', '.', 't', 'x', 't'});
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "report.txt");
	EXPECT_TRUE(result.lossless);
}

TEST(NameDecode, BmpTwoByteAccentedCharacterDecodes) {
	// "café" - the trailing 'e' has an acute accent (U+00E9), a 2-byte
	// UTF-8 sequence.
	const auto bytes = utf16LeBytes({'c', 'a', 'f', 0x00E9});
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "caf\xC3\xA9");
	EXPECT_TRUE(result.lossless);
}

TEST(NameDecode, BmpTwoByteHungarianCharacterDecodes) {
	// U+0151 (LATIN SMALL LETTER O WITH DOUBLE ACUTE, "ő").
	const auto bytes = utf16LeBytes({0x0151});
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "\xC5\x91");
	EXPECT_TRUE(result.lossless);
}

TEST(NameDecode, BmpThreeByteCharacterDecodes) {
	// U+20AC EURO SIGN - a 3-byte UTF-8 sequence.
	const auto bytes = utf16LeBytes({0x20AC});
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "\xE2\x82\xAC");
	EXPECT_TRUE(result.lossless);
}

TEST(NameDecode, SurrogatePairDecodesToFourByteUtf8) {
	// U+1D11E MUSICAL SYMBOL G CLEF: high surrogate D834, low surrogate DD1E.
	const auto bytes = utf16LeBytes({0xD834, 0xDD1E});
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "\xF0\x9D\x84\x9E");
	EXPECT_TRUE(result.lossless);
}

TEST(NameDecode, UnpairedHighSurrogateIsEscapedLossily) {
	const auto bytes = utf16LeBytes({0xD834});
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "%uD834");
	EXPECT_FALSE(result.lossless);
}

TEST(NameDecode, HighSurrogateFollowedByNonLowSurrogateIsEscapedLossily) {
	const auto bytes = utf16LeBytes({0xD834, 'x'});
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "%uD834x");
	EXPECT_FALSE(result.lossless);
}

TEST(NameDecode, ReversedSurrogatePairIsEscapedLossily) {
	// A low surrogate with no preceding high surrogate.
	const auto bytes = utf16LeBytes({0xDD1E, 0xD834});
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "%uDD1E%uD834");
	EXPECT_FALSE(result.lossless);
}

TEST(NameDecode, OddTrailingByteIsEscaped) {
	std::vector<std::byte> bytes = utf16LeBytes({'a'});
	bytes.push_back(std::byte{0xAB});
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "a%AB");
	EXPECT_FALSE(result.lossless);
}

TEST(NameDecode, SoleOddByteIsEscaped) {
	const std::vector<std::byte> bytes{std::byte{0x05}};
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "%05");
	EXPECT_FALSE(result.lossless);
}

TEST(NameDecode, EmbeddedNulUnitIsEscapedLossily) {
	const auto bytes = utf16LeBytes({'a', 0x0000, 'b'});
	const auto result = decodeUtf16Name(bytes);
	EXPECT_EQ(result.utf8, "a%u0000b");
	EXPECT_FALSE(result.lossless);
	// NUL never passes through as a raw byte - sanitize rejects it later
	// anyway, but the decoder itself must never emit one.
	EXPECT_EQ(result.utf8.find('\0'), std::string::npos);
}

} // namespace
