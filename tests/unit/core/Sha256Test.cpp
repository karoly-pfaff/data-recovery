// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/Sha256.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using revenant::sha256;
using revenant::Sha256;
using revenant::toHex;

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
	std::vector<std::byte> bytes;
	bytes.reserve(text.size());
	for (const char value : text) {
		bytes.push_back(std::bit_cast<std::byte>(value));
	}
	return bytes;
}

[[nodiscard]] std::string hashOf(std::string_view text) {
	return toHex(sha256(bytesOf(text)));
}

// Hashed in two calls split at `at`, which must land on the same digest as one.
[[nodiscard]] std::string hashInTwoParts(std::string_view text, std::size_t at) {
	const auto bytes = bytesOf(text);
	Sha256 hash;
	hash.update(std::span<const std::byte>{bytes}.first(at));
	hash.update(std::span<const std::byte>{bytes}.subspan(at));
	return toHex(hash.finish());
}

[[nodiscard]] std::string repeated(char value, std::size_t count) {
	std::string text;
	text.resize(count, value);
	return text;
}

// The published FIPS 180-4 vectors. Pinning to them is what makes this a
// SHA-256 rather than a hash that merely looks like one.
TEST(Sha256, MatchesThePublishedVectorForTheEmptyInput) {
	EXPECT_EQ(hashOf(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, MatchesThePublishedVectorForAbc) {
	EXPECT_EQ(hashOf("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// 56 bytes: the length that no longer fits its own padding, so the digest only
// comes out right if a second block is emitted.
TEST(Sha256, MatchesThePublishedVectorForTheTwoBlockMessage) {
	EXPECT_EQ(
		hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, StreamingAgreesWithOneShotWhereverTheChunksFall) {
	const auto text = repeated('a', 200);
	for (const std::size_t at :
		 {std::size_t{0}, std::size_t{1}, std::size_t{64}, std::size_t{199}}) {
		EXPECT_EQ(hashInTwoParts(text, at), hashOf(text)) << at;
	}
}

// One block exactly, and the two lengths either side of the padding boundary:
// the places a buffered hash gets it wrong.
TEST(Sha256, HandlesTheBlockAndPaddingBoundaries) {
	for (const std::size_t length : {std::size_t{55}, std::size_t{56}, std::size_t{64}}) {
		const auto text = repeated('x', length);
		EXPECT_EQ(hashInTwoParts(text, length / 2), hashOf(text)) << length;
	}
}

TEST(Sha256, HashesMoreThanOneBlockOfInput) {
	EXPECT_EQ(
		hashOf(repeated('a', 1000)),
		"41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST(Sha256, RendersSixtyFourLowercaseHexCharacters) {
	const auto hex = hashOf("abc");
	EXPECT_EQ(hex.size(), 64U);
	EXPECT_EQ(hex.find_first_not_of("0123456789abcdef"), std::string::npos);
}

} // namespace
