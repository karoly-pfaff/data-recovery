// SPDX-License-Identifier: GPL-3.0-or-later
#include "volume/PartitionLabel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "revenant/volume/GptPartitions.hpp"

namespace revenant::volume {

namespace {

constexpr std::string_view kUnknownGptLabel = "GPT partition";
constexpr std::string_view kHexDigits = "0123456789ABCDEF";
constexpr std::size_t kNibbleBits = 4;
constexpr std::size_t kLowNibble = 0x0F;

struct KnownMbrType {
	std::uint8_t code;
	std::string_view name;
};

// The type bytes worth naming. Short on purpose: a longer list would be a
// registry of every code ever issued, and offset and size already tell two
// partitions apart.
constexpr std::array<KnownMbrType, 10> kKnownMbrTypes{{
	{.code = 0x01, .name = "FAT12"},
	{.code = 0x04, .name = "FAT16"},
	{.code = 0x06, .name = "FAT16"},
	{.code = 0x07, .name = "NTFS/exFAT"},
	{.code = 0x0B, .name = "FAT32"},
	{.code = 0x0C, .name = "FAT32 (LBA)"},
	{.code = 0x82, .name = "Linux swap"},
	{.code = 0x83, .name = "Linux"},
	{.code = 0x8E, .name = "Linux LVM"},
	{.code = 0xEF, .name = "EFI system"},
}};

struct KnownGptType {
	// The GUID's sixteen bytes as hex, *in the order they sit on disk* — which
	// is not the canonical text form, because a GUID's first three fields are
	// stored little-endian. Written as digits rather than as sixteen numeric
	// literals because the formatter puts each literal on a line of its own, and
	// a table nobody can read is a table nobody can check.
	std::string_view onDiskHex;
	std::string_view name;
};

constexpr std::array<KnownGptType, 6> kKnownGptTypes{{
	{.onDiskHex = "28732AC11FF8D211BA4B00A0C93EC93B", .name = "EFI system"},
	{.onDiskHex = "A2A0D0EBE5B9334487C068B6B72699C7", .name = "Windows basic data"},
	{.onDiskHex = "16E3C9E35C0BB84D817DF92DF00215AE", .name = "Microsoft reserved"},
	{.onDiskHex = "A4BB94DED106404DA16ABFD50179D6AC", .name = "Windows recovery"},
	{.onDiskHex = "AF3DC60F838472478E793D69D8477DE4", .name = "Linux filesystem"},
	{.onDiskHex = "6DFD5706ABA4C34384E50933C84B4F4F", .name = "Linux swap"},
}};

// Uppercase, two digits per byte, in the order the bytes are given.
[[nodiscard]] std::string toHex(std::span<const std::byte> raw) {
	std::string text;
	for (const std::byte value : raw) {
		const auto number = std::to_integer<std::size_t>(value);
		text.push_back(kHexDigits.at(number >> kNibbleBits));
		text.push_back(kHexDigits.at(number & kLowNibble));
	}
	return text;
}

// Walked rather than searched with an algorithm: a std::array's iterator is a
// raw pointer on libstdc++ and a class on the MSVC STL, so holding one in `auto`
// is clean on one toolchain and a `readability-qualified-auto` error on the
// other (precedent: ZipCarver.cpp, RawName.cpp).
[[nodiscard]] std::string_view knownGptName(std::string_view onDiskHex) {
	for (const KnownGptType& known : kKnownGptTypes) {
		if (known.onDiskHex == onDiskHex) {
			return known.name;
		}
	}
	return kUnknownGptLabel;
}

} // namespace

std::string labelOfMbrType(std::uint8_t typeCode) {
	for (const KnownMbrType& known : kKnownMbrTypes) {
		if (known.code == typeCode) {
			return std::string{known.name};
		}
	}
	const std::array<std::byte, 1> raw{static_cast<std::byte>(typeCode)};
	return "type 0x" + toHex(raw);
}

std::string labelOfGptPartition(const GptPartition& partition) {
	if (!partition.name.empty()) {
		return partition.name;
	}
	return std::string{knownGptName(toHex(partition.typeGuid))};
}

} // namespace revenant::volume
