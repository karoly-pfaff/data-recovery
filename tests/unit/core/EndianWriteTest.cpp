// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "revenant/core/Endian.hpp"

namespace {

TEST(Endian, WritesLittleEndian64) {
	const auto bytes = revenant::toLittleEndian<std::uint64_t>(0x0123456789ABCDEFULL);
	EXPECT_EQ(bytes.at(0), std::byte{0xEF});
	EXPECT_EQ(bytes.at(7), std::byte{0x01});
}

TEST(Endian, LittleEndianRoundtrips) {
	const std::uint32_t original = 0xDEADBEEFU;
	EXPECT_EQ(
		revenant::fromLittleEndian<std::uint32_t>(
			revenant::toLittleEndian<std::uint32_t>(original)),
		original);
}

} // namespace
