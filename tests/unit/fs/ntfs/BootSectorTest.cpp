// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/BootSector.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace {

using revenant::ErrorCode;
using revenant::toLittleEndian;
using revenant::fs::ntfs::NtfsGeometry;
using revenant::fs::ntfs::parseBootSector;

constexpr std::size_t kBootSectorSize = 512;

void writeLeAt(std::vector<std::byte>& sector, std::uint64_t offset, auto value) {
	const auto bytes = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(bytes, sector.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::vector<std::byte> makeValidBootSector() {
	std::vector<std::byte> sector(kBootSectorSize);
	const std::array<std::byte, 8> oem{
		std::byte{'N'},
		std::byte{'T'},
		std::byte{'F'},
		std::byte{'S'},
		std::byte{' '},
		std::byte{' '},
		std::byte{' '},
		std::byte{' '}};
	std::ranges::copy(oem, sector.begin() + 0x03);
	writeLeAt(sector, 0x0B, static_cast<std::uint16_t>(512U));
	writeLeAt(sector, 0x0D, static_cast<std::uint8_t>(8U));
	writeLeAt(sector, 0x28, static_cast<std::uint64_t>(16384ULL));
	writeLeAt(sector, 0x30, static_cast<std::uint64_t>(4ULL));
	writeLeAt(sector, 0x40, static_cast<std::uint8_t>(0xF6U));
	writeLeAt(sector, 0x1FE, static_cast<std::uint16_t>(0xAA55U));
	return sector;
}

TEST(BootSector, ParsesValidSector) {
	const auto sector = makeValidBootSector();
	const auto result = parseBootSector(sector);
	ASSERT_TRUE(result.hasValue());
	const NtfsGeometry& g = result.value();
	EXPECT_EQ(g.bytesPerSector, 512U);
	EXPECT_EQ(g.bytesPerCluster, 4096U);
	EXPECT_EQ(g.totalClusters, 2048U);
	EXPECT_EQ(g.mftOffsetBytes, 16384U);
	EXPECT_EQ(g.bytesPerMftRecord, 1024U);
}

TEST(BootSector, TruncatedInputIsOutOfRange) {
	std::vector<std::byte> sector(100);
	const auto result = parseBootSector(sector);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kOutOfRange);
}

TEST(BootSector, BadOemIsInvalidArgument) {
	auto sector = makeValidBootSector();
	sector.at(0x03) = std::byte{'F'};
	const auto result = parseBootSector(sector);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
	EXPECT_EQ(result.error().offset, 0x03U);
}

TEST(BootSector, BadBytesPerSectorIsInvalidArgument) {
	auto sector = makeValidBootSector();
	writeLeAt(sector, 0x0B, static_cast<std::uint16_t>(256U));
	const auto result = parseBootSector(sector);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
	EXPECT_EQ(result.error().offset, 0x0BU);
}

TEST(BootSector, BadSectorsPerClusterIsInvalidArgument) {
	auto sector = makeValidBootSector();
	sector.at(0x0D) = std::byte{0x03};
	const auto result = parseBootSector(sector);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
	EXPECT_EQ(result.error().offset, 0x0DU);
}

TEST(BootSector, ZeroTotalSectorsIsInvalidArgument) {
	auto sector = makeValidBootSector();
	writeLeAt(sector, 0x28, static_cast<std::uint64_t>(0ULL));
	const auto result = parseBootSector(sector);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
	EXPECT_EQ(result.error().offset, 0x28U);
}

TEST(BootSector, MftClusterBeyondTotalIsInvalidArgument) {
	auto sector = makeValidBootSector();
	writeLeAt(sector, 0x30, static_cast<std::uint64_t>(2048ULL));
	const auto result = parseBootSector(sector);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
	EXPECT_EQ(result.error().offset, 0x30U);
}

TEST(BootSector, PositiveClustersPerMftRecordWorks) {
	auto sector = makeValidBootSector();
	writeLeAt(sector, 0x40, static_cast<std::uint8_t>(2U));
	const auto result = parseBootSector(sector);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().bytesPerMftRecord, 8192U);
}

TEST(BootSector, BadSignatureFirstByteIsInvalidArgument) {
	auto sector = makeValidBootSector();
	sector.at(0x1FE) = std::byte{0x00};
	const auto result = parseBootSector(sector);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
	EXPECT_EQ(result.error().offset, 0x1FEU);
}

TEST(BootSector, BadSignatureSecondByteIsInvalidArgument) {
	auto sector = makeValidBootSector();
	sector.at(0x1FF) = std::byte{0x00};
	const auto result = parseBootSector(sector);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
	EXPECT_EQ(result.error().offset, 0x1FEU);
}

} // namespace
