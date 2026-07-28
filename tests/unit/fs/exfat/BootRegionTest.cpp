// SPDX-License-Identifier: GPL-3.0-or-later
// story-0032: the exFAT boot region. exFAT states its geometry as log2
// exponents, so most of what is asserted here is that an exponent is judged
// before anything is shifted by it — and that the 53 zero bytes which tell
// exFAT from FAT are actually checked.
#include "revenant/fs/exfat/BootRegion.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "revenant/core/Error.hpp"

namespace {

using revenant::ErrorCode;
using revenant::toLittleEndian;
using revenant::fs::exfat::ExfatGeometry;
using revenant::fs::exfat::parseExfatBootSector;

constexpr std::size_t kBootSectorSize = 512;
constexpr std::size_t kNameOffset = 0x03;

// 512-byte sectors, 8 sectors per cluster, one 64-sector FAT at sector 128,
// and a cluster heap of 992 clusters starting at sector 256.
constexpr std::uint64_t kVolumeSectors = 8192;
constexpr std::uint32_t kFatSector = 128;
constexpr std::uint32_t kFatSectors = 64;
constexpr std::uint32_t kHeapSector = 256;
constexpr std::uint32_t kClusterCount = 992;
constexpr std::uint32_t kRootCluster = 2;
constexpr std::uint8_t kSectorShift = 9;
constexpr std::uint8_t kClusterShift = 3;

constexpr std::array<std::byte, 8> kExfatName{
	std::byte{'E'},
	std::byte{'X'},
	std::byte{'F'},
	std::byte{'A'},
	std::byte{'T'},
	std::byte{' '},
	std::byte{' '},
	std::byte{' '}};

void writeLe(std::vector<std::byte>& sector, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, sector.begin() + static_cast<std::ptrdiff_t>(offset));
}

void writeGeometry(std::vector<std::byte>& sector) {
	writeLe(sector, 0x48, kVolumeSectors);
	writeLe(sector, 0x50, kFatSector);
	writeLe(sector, 0x54, kFatSectors);
	writeLe(sector, 0x58, kHeapSector);
	writeLe(sector, 0x5C, kClusterCount);
	writeLe(sector, 0x60, kRootCluster);
}

void writeShifts(std::vector<std::byte>& sector) {
	writeLe(sector, 0x6C, kSectorShift);
	writeLe(sector, 0x6D, kClusterShift);
	writeLe(sector, 0x6E, static_cast<std::uint8_t>(1U));
	writeLe(sector, 0x1FE, static_cast<std::uint16_t>(0xAA55U));
}

[[nodiscard]] std::vector<std::byte> makeValidBootSector() {
	std::vector<std::byte> sector(kBootSectorSize, std::byte{0});
	std::ranges::copy(kExfatName, sector.begin() + static_cast<std::ptrdiff_t>(kNameOffset));
	writeGeometry(sector);
	writeShifts(sector);
	return sector;
}

struct Rejection {
	ErrorCode code;
	std::uint64_t offset;

	friend bool operator==(const Rejection&, const Rejection&) = default;
};

[[nodiscard]] Rejection rejectionOf(std::span<const std::byte> sector) {
	const auto parsed = parseExfatBootSector(sector);
	EXPECT_FALSE(parsed.hasValue());
	return parsed.hasValue()
			   ? Rejection{.code = ErrorCode::kNotFound, .offset = 0}
			   : Rejection{.code = parsed.error().code, .offset = parsed.error().offset};
}

[[nodiscard]] Rejection invalidAt(std::uint64_t offset) {
	return Rejection{.code = ErrorCode::kInvalidArgument, .offset = offset};
}

TEST(ExfatBootRegion, DerivesTheWholeGeometryFromAValidBootSector) {
	const auto parsed = parseExfatBootSector(makeValidBootSector());
	ASSERT_TRUE(parsed.hasValue());
	const ExfatGeometry& geometry = parsed.value();
	EXPECT_EQ(geometry.bytesPerSector, 512U);
	EXPECT_EQ(geometry.bytesPerCluster, 4096U);
	EXPECT_EQ(geometry.fatCount, 1U);
	EXPECT_EQ(geometry.fatOffsetBytes, 65536U);
	EXPECT_EQ(geometry.fatSizeBytes, 32768U);
	EXPECT_EQ(geometry.clusterHeapOffsetBytes, 131072U);
	EXPECT_EQ(geometry.totalClusters, kClusterCount);
	EXPECT_EQ(geometry.rootCluster, kRootCluster);
}

TEST(ExfatBootRegion, ASpanShorterThanASectorIsOutOfRange) {
	const std::vector<std::byte> stub(100, std::byte{0});
	EXPECT_EQ(
		rejectionOf(stub),
		(Rejection{.code = ErrorCode::kOutOfRange, .offset = stub.size()}));
}

TEST(ExfatBootRegion, AVolumeThatDoesNotSayExfatIsRejected) {
	auto sector = makeValidBootSector();
	sector.at(kNameOffset) = std::byte{'N'};
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x03));
}

// The 53 zero bytes are where a FAT BPB keeps its geometry. exFAT zeroes them
// on purpose so no driver can mistake one volume for the other, which makes
// them part of the identity — not padding.
TEST(ExfatBootRegion, ANonZeroByteAtTheStartOfTheMustBeZeroFieldIsRejected) {
	auto sector = makeValidBootSector();
	sector.at(0x0B) = std::byte{0x01};
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x0B));
}

TEST(ExfatBootRegion, ANonZeroByteAtTheEndOfTheMustBeZeroFieldIsRejected) {
	auto sector = makeValidBootSector();
	sector.at(0x3F) = std::byte{0x01};
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x0B));
}

// An exponent is judged before anything is shifted by it: an unchecked shift is
// undefined behaviour, not a large number.
TEST(ExfatBootRegion, ASectorShiftBelowTheAllowedRangeIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x6C, static_cast<std::uint8_t>(8U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x6C));
}

TEST(ExfatBootRegion, ASectorShiftAboveTheAllowedRangeIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x6C, static_cast<std::uint8_t>(13U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x6C));
}

TEST(ExfatBootRegion, AClusterShiftPastTheThirtyTwoMebibyteCapIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x6D, static_cast<std::uint8_t>(20U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x6D));
}

TEST(ExfatBootRegion, AFatCountOtherThanOneOrTwoIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x6E, static_cast<std::uint8_t>(3U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x6E));
}

TEST(ExfatBootRegion, AClusterHeapStartingPastTheVolumesEndIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x58, static_cast<std::uint32_t>(kVolumeSectors + 1));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x58));
}

TEST(ExfatBootRegion, AVolumeWithNoClustersIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x5C, static_cast<std::uint32_t>(0U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x5C));
}

TEST(ExfatBootRegion, ARootClusterOutsideTheHeapIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x60, static_cast<std::uint32_t>(kClusterCount + 2));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x60));
}

TEST(ExfatBootRegion, AMissingBootSignatureIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x1FE, static_cast<std::uint16_t>(0U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x1FE));
}

} // namespace
