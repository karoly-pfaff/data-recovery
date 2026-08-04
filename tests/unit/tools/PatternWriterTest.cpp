// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/PatternWriter.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <ostream>
#include <sstream>

#include "revenant/core/Error.hpp"
#include "support/FailingStream.hpp"

namespace {

using revenant::imagegen::fillSector;
using revenant::imagegen::kSectorBytes;
using revenant::imagegen::parsePattern;
using revenant::imagegen::Pattern;
using revenant::imagegen::writeFiller;
using revenant::imagegen::writeImage;
using revenant::testing::FailingBuf;

TEST(PatternWriter, ZeroPatternIsAllZeros) {
	std::array<std::byte, kSectorBytes> sector{std::byte{0xAA}};
	fillSector(sector, 3, Pattern::kZero);
	EXPECT_EQ(sector.at(0), std::byte{0});
	EXPECT_EQ(sector.at(kSectorBytes - 1), std::byte{0});
}

// Named for what it can witness. The sector term of `(n*512 + j) & 0xFF` is
// always a multiple of 256, so every sector holds the same 512 bytes and an
// offset self-describes modulo 256 — no assertion on these bytes can tell one
// sector from another, whatever it is called. The previous name said
// "EncodesAbsoluteOffset" and its expectations held with `lba` ignored
// entirely, which is a test that cannot fail (story-0606).
TEST(PatternWriter, CounterPatternCountsWithinTheSector) {
	std::array<std::byte, kSectorBytes> sector{};
	fillSector(sector, 2, Pattern::kCounter);
	EXPECT_EQ(sector.at(0), std::byte{0});
	EXPECT_EQ(sector.at(5), std::byte{5});
	EXPECT_EQ(sector.at(kSectorBytes - 1), std::byte{0xFF});
}

// The other half of the same fact, asserted rather than left to the reader:
// `kCounter` cannot tell one sector from another. Anything that needs to —
// `writeFiller`'s device-offset contract below — wants `kLbaTag` instead.
TEST(PatternWriter, CounterPatternIsTheSameBytesInEverySector) {
	std::array<std::byte, kSectorBytes> low{};
	std::array<std::byte, kSectorBytes> high{};
	fillSector(low, 0, Pattern::kCounter);
	fillSector(high, 4096, Pattern::kCounter);
	EXPECT_EQ(low, high);
}

// writeFiller's stated contract: the pattern is a function of the *device*
// offset, not of how much has been written so far. `kLbaTag` is the only
// pattern that can witness it — a filler that numbered its sectors from the
// write cursor would stamp 0 and 1 here instead of 3 and 4.
TEST(PatternWriter, FillerNumbersSectorsFromTheDeviceOffset) {
	std::ostringstream stream;
	const auto reached = writeFiller(stream, 3 * kSectorBytes, 5 * kSectorBytes, Pattern::kLbaTag);
	const auto written = stream.str();
	EXPECT_EQ(reached, 5 * kSectorBytes);
	ASSERT_EQ(written.size(), 2 * kSectorBytes);
	EXPECT_EQ(static_cast<unsigned char>(written.at(0)), 3U);
	EXPECT_EQ(static_cast<unsigned char>(written.at(kSectorBytes)), 4U);
}

// A writer's error offset has to be measured, not assumed: this is the only
// place a stream goes bad partway, and both writers report where they stopped.
TEST(PatternWriter, FillerReportsTheOffsetItReachedWhenTheStreamFails) {
	FailingBuf buf{kSectorBytes};
	std::ostream stream{&buf};
	EXPECT_EQ(writeFiller(stream, 0, 4 * kSectorBytes, Pattern::kZero), kSectorBytes);
}

// The rounding that answer implies, pinned separately: the filler advances a
// whole sector or not at all, so a stream that dies mid-sector reports the last
// boundary it fully took. A byte-exact cursor would say 600 here. Conservative
// is right for an error offset — the true end of a half-taken sector is not
// knowable, and the last known-good boundary is.
TEST(PatternWriter, FillerRoundsAPartlyTakenSectorDownToTheLastWholeOne) {
	FailingBuf buf{600};
	std::ostream stream{&buf};
	EXPECT_EQ(writeFiller(stream, 0, 4 * kSectorBytes, Pattern::kZero), kSectorBytes);
}

// A path that cannot be opened is the failure every caller can actually hit.
// The offset must stay 0 — `Error`'s contract is that it is meaningful or
// absent, and "we wrote all of it" is neither.
TEST(PatternWriter, AnUnopenableImageFailsWithNoOffset) {
	const auto path = std::filesystem::temp_directory_path() / "no-such-dir" / "x.img";
	const auto written = writeImage(path, 4096, Pattern::kZero);
	ASSERT_FALSE(written.hasValue());
	EXPECT_EQ(written.error().code, revenant::ErrorCode::kIoFailure);
	EXPECT_EQ(written.error().offset, 0U);
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
