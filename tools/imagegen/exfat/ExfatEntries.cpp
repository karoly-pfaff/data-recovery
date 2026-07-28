// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/exfat/ExfatEntries.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "imagegen/ByteWriter.hpp"

namespace revenant::imagegen::exfat {

namespace {

constexpr std::uint8_t kInUseBit = 0x80;
constexpr std::uint8_t kFileCode = 0x05;
constexpr std::uint8_t kStreamCode = 0x40;
constexpr std::uint8_t kNameCode = 0x41;
constexpr std::uint8_t kBitmapType = 0x81;

constexpr std::uint16_t kDirectoryAttribute = 0x0010;
constexpr std::uint8_t kNoFatChainFlag = 0x02;
constexpr std::uint8_t kAllocationPossibleFlag = 0x01;

// 2020-08-01 12:00:00, packed the way exFAT packs a DOS date and time into one
// 32-bit field.
constexpr std::uint32_t kFixtureStamp =
	(((2020U - 1980U) << 9U) | (8U << 5U) | 1U) << 16U | (12U << 11U);

[[nodiscard]] std::uint8_t typeByte(std::uint8_t code, bool live) {
	return static_cast<std::uint8_t>(live ? (code | kInUseBit) : code);
}

void putFileEntry(std::vector<std::byte>& slots, const SetSpec& spec) {
	putLe<std::uint8_t>(slots, 0x00, typeByte(kFileCode, spec.live));
	putLe<std::uint8_t>(slots, 0x01, 2);
	putLe<std::uint16_t>(slots, 0x04, spec.isDirectory ? kDirectoryAttribute : 0U);
	putLe<std::uint32_t>(slots, 0x08, kFixtureStamp);
	putLe<std::uint32_t>(slots, 0x0C, kFixtureStamp);
	putLe<std::uint32_t>(slots, 0x10, kFixtureStamp);
}

void putStreamEntry(std::vector<std::byte>& slots, const SetSpec& spec) {
	const auto flags = static_cast<std::uint8_t>(
		kAllocationPossibleFlag | (spec.contiguous ? kNoFatChainFlag : 0U));
	putLe<std::uint8_t>(slots, kSlotBytes + 0x00, typeByte(kStreamCode, spec.live));
	putLe<std::uint8_t>(slots, kSlotBytes + 0x01, flags);
	putLe<std::uint8_t>(slots, kSlotBytes + 0x03, static_cast<std::uint8_t>(spec.name.size()));
	putLe<std::uint64_t>(slots, kSlotBytes + 0x08, spec.sizeInBytes);
	putLe<std::uint32_t>(slots, kSlotBytes + 0x14, spec.firstCluster);
	putLe<std::uint64_t>(slots, kSlotBytes + 0x18, spec.sizeInBytes);
}

void putNameEntry(std::vector<std::byte>& slots, const SetSpec& spec) {
	putLe<std::uint8_t>(slots, (2 * kSlotBytes) + 0x00, typeByte(kNameCode, spec.live));
	for (std::size_t unit = 0; unit < spec.name.size(); ++unit) {
		putLe<std::uint16_t>(
			slots,
			(2 * kSlotBytes) + 0x02 + (unit * 2),
			static_cast<std::uint16_t>(spec.name.at(unit)));
	}
}

} // namespace

std::vector<std::byte> entrySet(const SetSpec& spec) {
	std::vector<std::byte> slots(3 * kSlotBytes, std::byte{0});
	putFileEntry(slots, spec);
	putStreamEntry(slots, spec);
	putNameEntry(slots, spec);
	return slots;
}

std::vector<std::byte> bitmapEntry(const BitmapSpec& spec) {
	std::vector<std::byte> slot(kSlotBytes, std::byte{0});
	putLe<std::uint8_t>(slot, 0x00, kBitmapType);
	putLe<std::uint32_t>(slot, 0x14, spec.firstCluster);
	putLe<std::uint64_t>(slot, 0x18, spec.lengthBytes);
	return slot;
}

} // namespace revenant::imagegen::exfat
