// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/CarveCorpus.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace {

using revenant::imagegen::buildCarveCorpus;
using revenant::imagegen::kCorpusJpegBytes;
using revenant::imagegen::kCorpusUnitBytes;

// The two headers a scan must find in every unit, spelled here rather than
// imported from the carvers: this fixture's job is to state what it plants,
// and asserting it against the parser that reads it would assert nothing.
constexpr std::array<std::byte, 2> kJpegStart{std::byte{0xFF}, std::byte{0xD8}};
constexpr std::array<std::byte, 8> kPngStart{
	std::byte{0x89},
	std::byte{0x50},
	std::byte{0x4E},
	std::byte{0x47},
	std::byte{0x0D},
	std::byte{0x0A},
	std::byte{0x1A},
	std::byte{0x0A}};

[[nodiscard]] bool
holdsAt(const std::vector<std::byte>& corpus, std::size_t offset, std::span<const std::byte> want) {
	const std::span<const std::byte> whole{corpus};
	return std::ranges::equal(whole.subspan(offset, want.size()), want);
}

TEST(CarveCorpus, IsExactlyTheRequestedSize) {
	EXPECT_EQ(buildCarveCorpus(kCorpusUnitBytes * 3).size(), kCorpusUnitBytes * 3);
}

// A benchmark asks for a byte count, not a unit count; the last unit is cut.
TEST(CarveCorpus, IsCutToASizeThatIsNotAWholeNumberOfUnits) {
	EXPECT_EQ(buildCarveCorpus(kCorpusUnitBytes + 1).size(), kCorpusUnitBytes + 1);
}

TEST(CarveCorpus, OpensEveryUnitWithAJpegHeader) {
	const auto corpus = buildCarveCorpus(kCorpusUnitBytes * 2);
	EXPECT_TRUE(holdsAt(corpus, 0, kJpegStart));
	EXPECT_TRUE(holdsAt(corpus, kCorpusUnitBytes, kJpegStart));
}

// The second header of each unit is the one that must be rejected: a PNG
// signature with no valid chunk behind it, so the scan pays for a candidate it
// cannot carve as well as for one it can.
TEST(CarveCorpus, PlantsARejectableHeaderBehindEachJpeg) {
	const auto corpus = buildCarveCorpus(kCorpusUnitBytes);
	EXPECT_TRUE(holdsAt(corpus, kCorpusJpegBytes, kPngStart));
}

TEST(CarveCorpus, IsIdenticalEveryBuild) {
	EXPECT_EQ(buildCarveCorpus(kCorpusUnitBytes), buildCarveCorpus(kCorpusUnitBytes));
}

} // namespace
