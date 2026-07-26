// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/PatternWriter.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

#include "revenant/core/Error.hpp"

namespace {

using revenant::imagegen::fillSector;
using revenant::imagegen::kSectorBytes;
using revenant::imagegen::parsePattern;
using revenant::imagegen::Pattern;

TEST(PatternWriter, ZeroPatternIsAllZeros) {
	std::array<std::byte, kSectorBytes> sector{std::byte{0xAA}};
	fillSector(sector, 3, Pattern::kZero);
	EXPECT_EQ(sector.at(0), std::byte{0});
	EXPECT_EQ(sector.at(kSectorBytes - 1), std::byte{0});
}

TEST(PatternWriter, CounterPatternEncodesAbsoluteOffset) {
	std::array<std::byte, kSectorBytes> sector{};
	fillSector(sector, 2, Pattern::kCounter);
	EXPECT_EQ(sector.at(0), static_cast<std::byte>((2U * kSectorBytes) & 0xFFU));
	EXPECT_EQ(sector.at(5), static_cast<std::byte>(((2U * kSectorBytes) + 5U) & 0xFFU));
}

TEST(PatternWriter, LbaTagPatternStampsSectorNumber) {
	std::array<std::byte, kSectorBytes> sector{std::byte{0xFF}};
	fillSector(sector, 0x0102030405060708ULL, Pattern::kLbaTag);
	EXPECT_EQ(sector.at(0), std::byte{0x08});
	EXPECT_EQ(sector.at(7), std::byte{0x01});
	EXPECT_EQ(sector.at(8), std::byte{0});
}

TEST(PatternWriter, ParsesKnownPatternNames) {
	EXPECT_EQ(parsePattern("zero").value(), Pattern::kZero);
	EXPECT_EQ(parsePattern("counter").value(), Pattern::kCounter);
	EXPECT_EQ(parsePattern("lba").value(), Pattern::kLbaTag);
}

TEST(PatternWriter, RejectsUnknownPatternName) {
	EXPECT_EQ(parsePattern("noise").error().code, revenant::ErrorCode::kInvalidArgument);
}

} // namespace
