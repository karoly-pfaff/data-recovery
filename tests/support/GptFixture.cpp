// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/GptFixture.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "revenant/core/Crc32.hpp"
#include "revenant/core/Endian.hpp"

namespace revenant::testing {

namespace {

// Spelled out here rather than taken from the parser's own constants: a fixture
// that borrowed them could not catch the parser getting them wrong.
constexpr std::array<std::byte, 8> kSignature{
	std::byte{'E'},
	std::byte{'F'},
	std::byte{'I'},
	std::byte{' '},
	std::byte{'P'},
	std::byte{'A'},
	std::byte{'R'},
	std::byte{'T'}};

constexpr std::size_t kHeaderBytes = 92;
constexpr std::size_t kEntryBytes = 128;
constexpr std::size_t kGuidBytes = 16;
constexpr std::size_t kCrcOffset = 0x10;
constexpr std::size_t kNameOffset = 0x38;
constexpr std::size_t kMbrTypeOffset = 0x1BE + 0x04;
constexpr std::size_t kMbrSignatureOffset = 0x1FE;
constexpr std::uint8_t kProtectiveType = 0xEE;

void writeLe(std::vector<std::byte>& bytes, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

void writeAt(std::vector<std::byte>& disk, std::size_t offset, std::span<const std::byte> raw) {
	std::ranges::copy(raw, disk.begin() + static_cast<std::ptrdiff_t>(offset));
}

void writeIdentity(std::vector<std::byte>& header) {
	writeAt(header, 0, std::span{kSignature});
	writeLe(header, 0x08, std::uint32_t{0x00010000});
	writeLe(header, 0x0C, static_cast<std::uint32_t>(kHeaderBytes));
}

void writePlacement(std::vector<std::byte>& header, const GptHeaderSpec& spec) {
	writeLe(header, 0x18, spec.myLba);
	writeLe(header, 0x20, spec.alternateLba);
	writeLe(header, 0x28, kFixtureFirstStart);
	writeLe(header, 0x30, spec.lastUsableLba);
}

void writeArrayLocation(std::vector<std::byte>& header, const GptHeaderSpec& spec) {
	writeLe(header, 0x48, spec.entryArrayLba);
	writeLe(header, 0x50, spec.entryCount);
	writeLe(header, 0x54, spec.entryBytes);
	writeLe(header, 0x58, spec.entryArrayCrc);
}

void writeName(std::vector<std::byte>& slot, std::string_view text) {
	std::size_t at = kNameOffset;
	for (const char letter : text) {
		writeLe(slot, at, static_cast<std::uint16_t>(letter));
		at += sizeof(std::uint16_t);
	}
}

// The four entries a fixture disk holds: two used, two left zero.
[[nodiscard]] std::vector<std::byte> makeEntryArray() {
	std::vector<std::byte> array(4 * kEntryBytes, std::byte{0});
	const auto first = makeGptEntry(
		GptEntrySpec{
			.typeSeed = 0xA1,
			.firstLba = kFixtureFirstStart,
			.lastLba = kFixtureFirstStart + kFixtureFirstSectors - 1,
			.name = "System"});
	const auto second = makeGptEntry(
		GptEntrySpec{
			.typeSeed = 0xB2,
			.firstLba = kFixtureSecondStart,
			.lastLba = kFixtureSecondStart + kFixtureSecondSectors - 1,
			.name = "Data"});
	writeAt(array, 0, first);
	writeAt(array, kEntryBytes, second);
	return array;
}

// Where one copy of the table sits, and where its twin is.
struct CopyAt {
	std::uint64_t headerLba = 0;
	std::uint64_t arrayLba = 0;
	std::uint64_t alternateLba = 0;
};

void writeCopy(
	std::vector<std::byte>& disk,
	const GptDiskShape& shape,
	const CopyAt& at,
	std::span<const std::byte> array) {
	const auto header = makeGptHeader(
		GptHeaderSpec{
			.myLba = at.headerLba,
			.alternateLba = at.alternateLba,
			.entryArrayLba = at.arrayLba,
			.lastUsableLba = shape.sectorCount - kFixtureFirstStart,
			.entryArrayCrc = crc32(array)});
	writeAt(disk, static_cast<std::size_t>(at.arrayLba * shape.sectorSize), array);
	writeAt(disk, static_cast<std::size_t>(at.headerLba * shape.sectorSize), header);
}

// Sector 0 of a GPT disk: one entry covering everything, so that a tool which
// only understands the old table keeps away.
void writeProtectiveMbr(std::vector<std::byte>& disk) {
	writeLe(disk, kMbrTypeOffset, kProtectiveType);
	writeLe(disk, kMbrTypeOffset + 0x04, std::uint32_t{1});
	writeLe(disk, kMbrTypeOffset + 0x08, std::uint32_t{0xFFFFFFFF});
	writeLe(disk, kMbrSignatureOffset, std::uint16_t{0xAA55});
}

} // namespace

void signGptHeader(std::vector<std::byte>& header) {
	writeLe(header, kCrcOffset, std::uint32_t{0});
	writeLe(header, kCrcOffset, crc32(std::span{header}));
}

std::vector<std::byte> makeGptHeader(const GptHeaderSpec& spec) {
	std::vector<std::byte> header(kHeaderBytes, std::byte{0});
	writeIdentity(header);
	writePlacement(header, spec);
	writeArrayLocation(header, spec);
	signGptHeader(header);
	return header;
}

std::vector<std::byte> gptTypeGuid(std::uint8_t typeSeed) {
	return std::vector<std::byte>(kGuidBytes, static_cast<std::byte>(typeSeed));
}

std::vector<std::byte> makeGptEntry(const GptEntrySpec& spec) {
	std::vector<std::byte> slot(kEntryBytes, std::byte{0});
	writeAt(slot, 0, gptTypeGuid(spec.typeSeed));
	writeLe(slot, 0x20, spec.firstLba);
	writeLe(slot, 0x28, spec.lastLba);
	writeName(slot, spec.name);
	return slot;
}

std::vector<std::byte> makeGptDisk(const GptDiskShape& shape) {
	std::vector<std::byte> disk(shape.sectorCount * shape.sectorSize, std::byte{0});
	const auto array = makeEntryArray();
	const auto backupLba = shape.sectorCount - 1;
	writeProtectiveMbr(disk);
	writeCopy(
		disk,
		shape,
		CopyAt{
			.headerLba = kFixtureHeaderLba,
			.arrayLba = kFixtureArrayLba,
			.alternateLba = backupLba},
		array);
	writeCopy(
		disk,
		shape,
		CopyAt{
			.headerLba = backupLba,
			.arrayLba = backupLba - 1,
			.alternateLba = kFixtureHeaderLba},
		array);
	return disk;
}

} // namespace revenant::testing
