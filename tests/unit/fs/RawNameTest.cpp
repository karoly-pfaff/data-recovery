// SPDX-License-Identifier: GPL-3.0-or-later
// story-0306: ext4 stores names as raw bytes with no enforced encoding
// (ADR-0010), so decoding one is a validation rather than a transcoding. Valid
// UTF-8 — what a Linux volume almost always holds — passes through untouched;
// everything else is escaped byte by byte, so nothing is dropped and nothing is
// guessed at.
#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

#include "revenant/fs/NameDecode.hpp"

namespace {

using revenant::fs::decodeRawName;

[[nodiscard]] std::vector<std::byte> rawBytes(std::initializer_list<unsigned> values) {
	std::vector<std::byte> bytes;
	bytes.reserve(values.size());
	for (const unsigned value : values) {
		bytes.push_back(static_cast<std::byte>(value));
	}
	return bytes;
}

[[nodiscard]] std::vector<std::byte> textBytes(std::string_view text) {
	std::vector<std::byte> bytes;
	bytes.reserve(text.size());
	for (const char letter : text) {
		bytes.push_back(static_cast<std::byte>(letter));
	}
	return bytes;
}

TEST(RawName, EmptyInputDecodesToAnEmptyLosslessName) {
	const auto decoded = decodeRawName(std::span<const std::byte>{});
	EXPECT_EQ(decoded.utf8, "");
	EXPECT_TRUE(decoded.lossless);
}

TEST(RawName, AsciiPassesThroughUntouched) {
	const auto decoded = decodeRawName(textBytes("report.txt"));
	EXPECT_EQ(decoded.utf8, "report.txt");
	EXPECT_TRUE(decoded.lossless);
}

// The common case on a real volume: the name is already UTF-8 and the decoder's
// job is to confirm it, not to convert it.
TEST(RawName, WellFormedMultiByteUtf8PassesThroughUntouched) {
	const auto decoded = decodeRawName(textBytes("\xC5\x91sszefoglal\xC3\xB3.txt"));
	EXPECT_EQ(decoded.utf8, "\xC5\x91sszefoglal\xC3\xB3.txt");
	EXPECT_TRUE(decoded.lossless);
}

TEST(RawName, AFourByteSequencePassesThroughUntouched) {
	// U+1F5BC FRAME WITH PICTURE.
	const auto decoded = decodeRawName(rawBytes({0xF0, 0x9F, 0x96, 0xBC}));
	EXPECT_EQ(decoded.utf8, "\xF0\x9F\x96\xBC");
	EXPECT_TRUE(decoded.lossless);
}

// A lead byte whose continuation never arrives is one bad byte, not a licence to
// swallow the rest of the name.
TEST(RawName, AnInvalidSequenceIsEscapedByteByByteAndTheRestSurvives) {
	const auto decoded = decodeRawName(rawBytes({'a', 0xC3, 'b'}));
	EXPECT_EQ(decoded.utf8, "a%C3b");
	EXPECT_FALSE(decoded.lossless);
}

TEST(RawName, ATruncatedSequenceAtTheEndIsEscaped) {
	const auto decoded = decodeRawName(rawBytes({0xE2, 0x82}));
	EXPECT_EQ(decoded.utf8, "%E2%82");
	EXPECT_FALSE(decoded.lossless);
}

// `0xC0 0xAF` is `/` written the long way — the classic way past a path check.
TEST(RawName, AnOverlongEncodingIsEscapedRatherThanAccepted) {
	const auto decoded = decodeRawName(rawBytes({0xC0, 0xAF}));
	EXPECT_EQ(decoded.utf8, "%C0%AF");
	EXPECT_FALSE(decoded.lossless);
}

TEST(RawName, ASurrogateEncodedAsUtf8IsEscaped) {
	// U+D800 as CESU-8: never valid UTF-8.
	const auto decoded = decodeRawName(rawBytes({0xED, 0xA0, 0x80}));
	EXPECT_EQ(decoded.utf8, "%ED%A0%80");
	EXPECT_FALSE(decoded.lossless);
}

TEST(RawName, ACodePointPastTheUnicodeRangeIsEscaped) {
	const auto decoded = decodeRawName(rawBytes({0xF5, 0x80, 0x80, 0x80}));
	EXPECT_EQ(decoded.utf8, "%F5%80%80%80");
	EXPECT_FALSE(decoded.lossless);
}

TEST(RawName, ANulByteNeverPassesThroughAsItself) {
	const auto decoded = decodeRawName(rawBytes({'a', 0x00, 'b'}));
	EXPECT_EQ(decoded.utf8, "a%00b");
	EXPECT_FALSE(decoded.lossless);
}

TEST(RawName, AControlByteIsEscaped) {
	const auto decoded = decodeRawName(rawBytes({'a', 0x0A, 0x7F}));
	EXPECT_EQ(decoded.utf8, "a%0A%7F");
	EXPECT_FALSE(decoded.lossless);
}

// ext4 cannot store a `/` in a name, but a *deleted* entry's bytes are whatever
// happens to be lying in the directory's hole — so the decoder is what stops one
// from splitting a volume-relative path.
TEST(RawName, ASlashIsEscapedBecauseItWouldSplitAPath) {
	const auto decoded = decodeRawName(textBytes("a/b"));
	EXPECT_EQ(decoded.utf8, "a%2Fb");
	EXPECT_FALSE(decoded.lossless);
}

TEST(RawName, APercentIsEscapedSoNoEscapeIsAmbiguous) {
	const auto decoded = decodeRawName(textBytes("100%"));
	EXPECT_EQ(decoded.utf8, "100%25");
	EXPECT_FALSE(decoded.lossless);
}

} // namespace
