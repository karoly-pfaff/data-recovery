// SPDX-License-Identifier: GPL-3.0-or-later
// story-0302: the FAT32 BPB, validated field by field. Every rejection asserts
// the byte offset it names as well as the code, because "which field" is what
// makes a parse failure diagnosable on a damaged volume.
#include "revenant/fs/fat/BootSector.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::toLittleEndian;
using revenant::fs::fat::Fat32Geometry;
using revenant::fs::fat::parseFat32BootSector;
using revenant::testing::invalidAt;
using revenant::testing::outOfRangeAt;
using revenant::testing::Rejection;

constexpr std::size_t kBootSectorSize = 512;
constexpr std::size_t kFilSysTypeOffset = 0x52;

// The fixture volume's shape, small enough to reason about by hand: 512-byte
// sectors, 4 sectors per cluster, 32 reserved sectors, two 64-sector FATs. Data
// therefore begins at sector 160, and 984 clusters fit after it.
constexpr std::uint16_t kBytesPerSector = 512;
constexpr std::uint8_t kSectorsPerCluster = 4;
constexpr std::uint16_t kReservedSectors = 32;
constexpr std::uint8_t kFatCount = 2;
constexpr std::uint32_t kFatSectors = 64;
constexpr std::uint32_t kTotalSectors = 4096;
constexpr std::uint32_t kRootCluster = 2;

constexpr std::uint64_t kExpectedFatOffset = 32ULL * 512;
constexpr std::uint64_t kExpectedFatSize = 64ULL * 512;
constexpr std::uint64_t kExpectedDataOffset = 160ULL * 512;
constexpr std::uint64_t kExpectedClusters = 984;
constexpr std::uint32_t kExpectedClusterBytes = 2048;

// Data starts at sector 160, so this is the smallest volume that clears the
// 65525-cluster minimum at 4 sectors per cluster.
constexpr std::uint32_t kConformingTotalSectors = 160 + (65525 * 4);

constexpr std::array<std::byte, 8> kFat32Type{
	std::byte{'F'},
	std::byte{'A'},
	std::byte{'T'},
	std::byte{'3'},
	std::byte{'2'},
	std::byte{' '},
	std::byte{' '},
	std::byte{' '}};

constexpr std::array<std::byte, 8> kFat16Type{
	std::byte{'F'},
	std::byte{'A'},
	std::byte{'T'},
	std::byte{'1'},
	std::byte{'6'},
	std::byte{' '},
	std::byte{' '},
	std::byte{' '}};

void writeLe(std::vector<std::byte>& sector, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, sector.begin() + static_cast<std::ptrdiff_t>(offset));
}

void writeFilSysType(std::vector<std::byte>& sector, std::span<const std::byte> type) {
	std::ranges::copy(type, sector.begin() + static_cast<std::ptrdiff_t>(kFilSysTypeOffset));
}

void writeClusterShape(std::vector<std::byte>& sector) {
	writeLe(sector, 0x0B, kBytesPerSector);
	writeLe(sector, 0x0D, kSectorsPerCluster);
	writeLe(sector, 0x0E, kReservedSectors);
	writeLe(sector, 0x10, kFatCount);
}

void writeVolumeShape(std::vector<std::byte>& sector) {
	writeLe(sector, 0x20, kTotalSectors);
	writeLe(sector, 0x24, kFatSectors);
	writeLe(sector, 0x2C, kRootCluster);
	writeFilSysType(sector, kFat32Type);
	writeLe(sector, 0x1FE, static_cast<std::uint16_t>(0xAA55U));
}

[[nodiscard]] std::vector<std::byte> makeValidBootSector() {
	std::vector<std::byte> sector(kBootSectorSize, std::byte{0});
	writeClusterShape(sector);
	writeVolumeShape(sector);
	return sector;
}

[[nodiscard]] Rejection rejectionOf(std::span<const std::byte> sector) {
	return revenant::testing::rejectionOf(parseFat32BootSector(sector));
}

TEST(Fat32BootSector, DerivesTheWholeGeometryFromAValidBpb) {
	const auto parsed = parseFat32BootSector(makeValidBootSector());
	ASSERT_TRUE(parsed.hasValue());
	const Fat32Geometry& geometry = parsed.value();
	EXPECT_EQ(geometry.bytesPerSector, kBytesPerSector);
	EXPECT_EQ(geometry.bytesPerCluster, kExpectedClusterBytes);
	EXPECT_EQ(geometry.fatCount, kFatCount);
	EXPECT_EQ(geometry.fatOffsetBytes, kExpectedFatOffset);
	EXPECT_EQ(geometry.fatSizeBytes, kExpectedFatSize);
	EXPECT_EQ(geometry.dataOffsetBytes, kExpectedDataOffset);
	EXPECT_EQ(geometry.totalClusters, kExpectedClusters);
	EXPECT_EQ(geometry.rootCluster, kRootCluster);
}

// The fixture volume is far below the count that makes a volume FAT32 by the
// specification's own test. It is still readable, so the parser says so rather
// than refusing it — the caller is what warns.
TEST(Fat32BootSector, AVolumeBelowTheFat32ClusterMinimumSaysSo) {
	const auto parsed = parseFat32BootSector(makeValidBootSector());
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_TRUE(parsed.value().belowClusterMinimum);
}

TEST(Fat32BootSector, AConformingVolumeDoesNotRaiseTheWarning) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x20, kConformingTotalSectors);
	const auto parsed = parseFat32BootSector(sector);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_GE(parsed.value().totalClusters, revenant::fs::fat::kFat32MinimumClusters);
	EXPECT_FALSE(parsed.value().belowClusterMinimum);
}

TEST(Fat32BootSector, ASpanShorterThanASectorIsOutOfRange) {
	const std::vector<std::byte> stub(100, std::byte{0});
	EXPECT_EQ(rejectionOf(stub), outOfRangeAt(stub.size()));
}

// The type string is what the mounter will recognize the volume by; here it is
// simply the first field, rejected like any other.
TEST(Fat32BootSector, AVolumeThatDoesNotSayFat32IsRejected) {
	auto sector = makeValidBootSector();
	writeFilSysType(sector, kFat16Type);
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x52));
}

TEST(Fat32BootSector, ABytesPerSectorOffTheAllowedListIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x0B, static_cast<std::uint16_t>(256U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x0B));
}

TEST(Fat32BootSector, ASectorsPerClusterThatIsNotAPowerOfTwoIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x0D, static_cast<std::uint8_t>(3U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x0D));
}

TEST(Fat32BootSector, AZeroReservedSectorCountIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x0E, static_cast<std::uint16_t>(0U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x0E));
}

TEST(Fat32BootSector, AZeroFatCountIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x10, static_cast<std::uint8_t>(0U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x10));
}

// Three fields say "this is FAT12/16, not FAT32". Each must be zero, and each
// names its own offset when it is not.
TEST(Fat32BootSector, ARootEntryCountBelongsToFat16AndIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x11, static_cast<std::uint16_t>(512U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x11));
}

TEST(Fat32BootSector, ASixteenBitTotalSectorCountBelongsToFat16AndIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x13, static_cast<std::uint16_t>(4096U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x13));
}

TEST(Fat32BootSector, ASixteenBitFatSizeBelongsToFat16AndIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x16, static_cast<std::uint16_t>(64U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x16));
}

TEST(Fat32BootSector, AZeroFatSizeIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x24, static_cast<std::uint32_t>(0U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x24));
}

TEST(Fat32BootSector, AZeroTotalSectorCountIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x20, static_cast<std::uint32_t>(0U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x20));
}

// The reserved region and the FATs have to fit inside the volume before there
// can be a data region at all.
TEST(Fat32BootSector, AVolumeTooSmallToHoldItsOwnFatsIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x20, static_cast<std::uint32_t>(64U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x20));
}

TEST(Fat32BootSector, AVolumeWithNoWholeDataClusterIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x20, static_cast<std::uint32_t>(161U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x20));
}

TEST(Fat32BootSector, ARootClusterBelowTheFirstDataClusterIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x2C, static_cast<std::uint32_t>(1U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x2C));
}

TEST(Fat32BootSector, ARootClusterPastTheDataRegionIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x2C, static_cast<std::uint32_t>(kExpectedClusters + 2));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x2C));
}

// The last data cluster is a valid root, which is what makes the bound above
// an off-by-one worth pinning down.
TEST(Fat32BootSector, TheLastDataClusterIsAValidRoot) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x2C, static_cast<std::uint32_t>(kExpectedClusters + 1));
	const auto parsed = parseFat32BootSector(sector);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_EQ(parsed.value().rootCluster, kExpectedClusters + 1);
}

TEST(Fat32BootSector, AMissingBootSignatureIsRejected) {
	auto sector = makeValidBootSector();
	writeLe(sector, 0x1FE, static_cast<std::uint16_t>(0U));
	EXPECT_EQ(rejectionOf(sector), invalidAt(0x1FE));
}

} // namespace
