// SPDX-License-Identifier: GPL-3.0-or-later
// story-0302: the 8.3 name, decoded. What is asserted here is mostly about
// what the decoder refuses to invent — a lost first character, an unknown code
// page, a byte that would split a path.
#include "fs/fat/ShortName.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/Utf16Name.hpp"

namespace {

using revenant::DecodedName;
using revenant::fs::fat::decodeShortName;

constexpr std::size_t kNameBytes = 11;
constexpr std::uint8_t kNoCaseFlags = 0;
constexpr std::uint8_t kLowerBase = 0x08;
constexpr std::uint8_t kLowerExtension = 0x10;

// The 11 raw bytes of a short name, written the way a directory holds them:
// eight for the base and three for the extension, both space-padded.
[[nodiscard]] std::vector<std::byte> rawName(std::string_view eleven) {
	std::vector<std::byte> raw(kNameBytes, std::byte{0x20});
	std::ranges::transform(eleven, raw.begin(), [](char c) { return static_cast<std::byte>(c); });
	return raw;
}

[[nodiscard]] DecodedName decode(std::string_view eleven, std::uint8_t flags = kNoCaseFlags) {
	return decodeShortName(rawName(eleven), flags, false);
}

[[nodiscard]] DecodedName decodeDeleted(std::string_view eleven) {
	return decodeShortName(rawName(eleven), kNoCaseFlags, true);
}

TEST(FatShortName, JoinsTheBaseAndExtensionWithADot) {
	const auto name = decode("KEEP    JPG");
	EXPECT_EQ(name.utf8, "KEEP.JPG");
	EXPECT_TRUE(name.lossless);
}

TEST(FatShortName, DropsThePaddingRatherThanTheName) {
	EXPECT_EQ(decode("A       B  ").utf8, "A.B");
}

TEST(FatShortName, ANameWithNoExtensionGetsNoTrailingDot) {
	EXPECT_EQ(decode("README     ").utf8, "README");
}

// The case flags are the only record of the name's real case: the 8.3 field
// itself is always upper.
TEST(FatShortName, TheCaseFlagsLowerEachHalfOnItsOwn) {
	EXPECT_EQ(decode("KEEP    JPG", kLowerBase).utf8, "keep.JPG");
	EXPECT_EQ(decode("KEEP    JPG", kLowerExtension).utf8, "KEEP.jpg");
	EXPECT_EQ(decode("KEEP    JPG", kLowerBase | kLowerExtension).utf8, "keep.jpg");
}

// Nothing on the volume holds the character 0xE5 overwrote, so the name says
// so instead of guessing at it.
TEST(FatShortName, ADeletedNameLosesItsFirstCharacterAndSaysSo) {
	const auto name = decodeDeleted(
		"\xE5"
		"EEP    JPG");
	EXPECT_EQ(name.utf8, "_EEP.JPG");
	EXPECT_FALSE(name.lossless);
}

// 0x05 is how a live name whose real first character *is* 0xE5 is stored, so
// this one is restored rather than treated as a deletion — and the restored
// byte is outside ASCII, so it is escaped like any other.
TEST(FatShortName, AnEscapedFirstCharacterIsRestoredNotReadAsADeletion) {
	const auto name = decode(
		"\x05"
		"EEP    JPG");
	EXPECT_EQ(name.utf8, "%E5EEP.JPG");
	EXPECT_FALSE(name.lossless);
}

// The volume does not record its OEM code page, so a high byte is escaped
// rather than mistranslated into whichever one the reader happens to assume.
TEST(FatShortName, AByteOutsideAsciiIsEscapedRatherThanGuessedAt) {
	const auto name = decode("CAF\xE9    TXT");
	EXPECT_EQ(name.utf8, "CAF%E9.TXT");
	EXPECT_FALSE(name.lossless);
}

// A path separator inside a name would silently become a directory level.
TEST(FatShortName, ASeparatorInsideANameCannotPassThrough) {
	EXPECT_EQ(decode("A/B     TXT").utf8, "A%2FB.TXT");
}

// Escapes have to be unambiguous, so a literal percent is escaped too.
TEST(FatShortName, ALiteralPercentIsEscapedSoAnEscapeStaysReadable) {
	EXPECT_EQ(decode("A%B     TXT").utf8, "A%25B.TXT");
}

TEST(FatShortName, ATruncatedNameFieldDecodesToNothing) {
	const std::array<std::byte, 4> tooShort{};
	const auto name = decodeShortName(tooShort, kNoCaseFlags, false);
	EXPECT_TRUE(name.utf8.empty());
	EXPECT_FALSE(name.lossless);
}

} // namespace
