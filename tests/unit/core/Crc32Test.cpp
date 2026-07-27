// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/Crc32.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace {

using revenant::crc32;

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
	const auto raw = std::as_bytes(std::span{text.data(), text.size()});
	return std::vector<std::byte>{raw.begin(), raw.end()};
}

// The published IEEE 802.3 check values, so everything built on this — PNG
// chunk CRCs, ZIP entry CRCs — rests on an independently verified primitive.
TEST(Crc32, MatchesThePublishedCheckVector) {
	EXPECT_EQ(crc32(bytesOf("123456789")), 0xCBF43926U);
}

TEST(Crc32, EmptyInputIsZero) {
	EXPECT_EQ(crc32(std::span<const std::byte>{}), 0U);
}

TEST(Crc32, SingleByteVectors) {
	EXPECT_EQ(crc32(bytesOf("a")), 0xE8B7BE43U);
	EXPECT_EQ(crc32(bytesOf("abc")), 0x352441C2U);
}

TEST(Crc32, DiffersWhenOneBitFlips) {
	EXPECT_NE(crc32(bytesOf("revenant")), crc32(bytesOf("revenanu")));
}

} // namespace
