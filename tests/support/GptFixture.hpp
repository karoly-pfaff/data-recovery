// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Hand-built GPT bytes: one header, one entry slot, and a whole disk carrying
// both copies of the table. Shared by the parser tests and the device-level
// tests so that neither restates the layout the other assumes.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace revenant::testing {

// Where a fixture disk puts the primary copy.
inline constexpr std::uint64_t kFixtureHeaderLba = 1;
inline constexpr std::uint64_t kFixtureArrayLba = 2;

// The two used entries a fixture disk holds, in sectors.
inline constexpr std::uint64_t kFixtureFirstStart = 34;
inline constexpr std::uint64_t kFixtureFirstSectors = 1000;
inline constexpr std::uint64_t kFixtureSecondStart = 1034;
inline constexpr std::uint64_t kFixtureSecondSectors = 1000;

// The header fields a fixture varies; everything else is fixed and valid.
struct GptHeaderSpec {
	std::uint64_t myLba = kFixtureHeaderLba;
	std::uint64_t alternateLba = 4095;
	std::uint64_t entryArrayLba = kFixtureArrayLba;
	std::uint64_t lastUsableLba = 4062;
	std::uint32_t entryCount = 4;
	std::uint32_t entryBytes = 128;
	std::uint32_t entryArrayCrc = 0;
};

// 92 bytes, checksummed last, as a writer does it.
[[nodiscard]] std::vector<std::byte> makeGptHeader(const GptHeaderSpec& spec);

// Recomputes a header's own checksum over its current bytes. A test that
// rewrites a field calls this, so that what the parser rejects is the field
// rather than the checksum the rewrite invalidated.
void signGptHeader(std::vector<std::byte>& header);

// One 128-byte entry slot. A `typeSeed` of zero leaves the type GUID sixteen
// zero bytes, which is the only "unused" GPT has.
struct GptEntrySpec {
	std::uint8_t typeSeed = 0;
	std::uint64_t firstLba = 0;
	std::uint64_t lastLba = 0;
	std::string_view name;
};

[[nodiscard]] std::vector<std::byte> makeGptEntry(const GptEntrySpec& spec);

// The sixteen bytes `typeSeed` produces, for asserting a round trip.
[[nodiscard]] std::vector<std::byte> gptTypeGuid(std::uint8_t typeSeed);

// A whole synthetic GPT disk: a protective MBR in sector 0, a primary header and
// its entry array, and a backup copy in the last two sectors. Two of the four
// entries are used.
struct GptDiskShape {
	std::uint32_t sectorSize = 512;
	std::uint64_t sectorCount = 4096;
};

[[nodiscard]] std::vector<std::byte> makeGptDisk(const GptDiskShape& shape);

} // namespace revenant::testing
